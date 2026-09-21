#include "Combat/AetherProjectile.h"
#include "Combat/AetherCombat.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
namespace
{
void Tint(UStaticMeshComponent* Mesh,FLinearColor Color)
{
    auto* Mat=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
    if(!Mat)Mat=Mesh->CreateAndSetMaterialInstanceDynamic(0);
    if(Mat)Mat->SetVectorParameterValue(TEXT("Color"),Color);
}
}
AAetherProjectile::AAetherProjectile()
{
    PrimaryActorTick.bCanEverTick = true; bReplicates = true; SetReplicateMovement(true); SetNetUpdateFrequency(30);
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FireCore")); SetRootComponent(Visual);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Visual->SetStaticMesh(Sphere.Object); Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetRelativeScale3D(FVector(.28));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Surface(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (Surface.Succeeded()) Visual->SetMaterial(0,Surface.Object);
}
void AAetherProjectile::BeginPlay() { Super::BeginPlay(); Tint(Visual, FLinearColor(1,.12f,.005f)); }
void AAetherProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AAetherProjectile, HeatJ); }
void AAetherProjectile::IntegrateWeather(double& Heat, FVector& V, const Reactive::FEnvironment& E, float Dt)
{ Heat = FMath::Max(0.0, Heat - (1500 + E.RainKgPerM2Sec * 220000) * Dt); V += E.WindMPerSec * 40 * Dt; }
void AAetherProjectile::Tick(float Dt)
{
    Super::Tick(Dt); Visual->SetRelativeScale3D(FVector(.08 + .2 * FMath::Clamp(HeatJ / 60000, 0.0, 1.0)));
    if (!HasAuthority()) return;
    Age += Dt;
    if (const auto* S = GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()) IntegrateWeather(HeatJ, VelocityCm, S->GetSimulation()->GetEnvironment(), Dt);
    if (Age > 4 || HeatJ < 500) { Destroy(); return; }
    FHitResult Hit; const FVector End = GetActorLocation() + VelocityCm * Dt;
    FCollisionQueryParams P(SCENE_QUERY_STAT(AetherProjectile), false, this); P.AddIgnoredActor(GetOwner());
    if (GetWorld()->SweepSingleByChannel(Hit,GetActorLocation(),End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(12),P))
    {
        if(auto* Source=Cast<AAetherCharacter>(GetOwner());Source&&Source->Fighter==EAetherFighter::Player)if(auto* Other=Cast<AAetherCharacter>(Hit.GetActor());Other&&Other->Fighter==EAetherFighter::Player){Destroy();return;}
        if (AActor* A = Hit.GetActor()) if (auto* B = A->FindComponentByClass<UReactiveBodyComponent>())
        { FReactiveStimulus S; S.SourceActor = GetOwner(); S.HeatJ = HeatJ; S.ImpulseNs = VelocityCm.GetSafeNormal() * 2; B->Inject(S); }
        Destroy(); return;
    }
    SetActorLocation(End);
}
