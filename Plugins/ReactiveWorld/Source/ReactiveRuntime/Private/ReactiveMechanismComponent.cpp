#include "ReactiveMechanismComponent.h"
#include "ReactiveBodyComponent.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

UReactiveMechanismComponent::UReactiveMechanismComponent()
{ PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.05f;SetIsReplicatedByDefault(true); }
void UReactiveMechanismComponent::BeginPlay()
{
    Super::BeginPlay();Primitive=Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
    if(GetOwner()->HasAuthority()&&Primitive.IsValid()&&bImpactDamage)
    {Primitive->SetNotifyRigidBodyCollision(true);Primitive->OnComponentHit.AddDynamic(this,&UReactiveMechanismComponent::Hit);}
}
void UReactiveMechanismComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(UReactiveMechanismComponent,bReleased);DOREPLIFETIME(UReactiveMechanismComponent,bGateOpen);
    DOREPLIFETIME(UReactiveMechanismComponent,RemainingEnergyJ);DOREPLIFETIME(UReactiveMechanismComponent,bPowerEnabled);DOREPLIFETIME(UReactiveMechanismComponent,SourceAge);
}
void UReactiveMechanismComponent::ReleaseSupport()
{
    if(bReleased)return;bReleased=true;
    if(Constraint)Constraint->BreakConstraint();
    if(Primitive.IsValid())Primitive->SetSimulatePhysics(true);
    if(auto* B=GetOwner()->FindComponentByClass<UReactiveBodyComponent>())
        if(auto* W=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>())W->TrackMovement(B->GetBodyId());
    GetOwner()->ForceNetUpdate();
}
void UReactiveMechanismComponent::RestoreMechanism(bool Released,double EnergyJ,double Age,bool Enabled)
{
    if(!GetOwner()->HasAuthority()||!FMath::IsFinite(EnergyJ)||EnergyJ<0||!FMath::IsFinite(Age)||Age<0)return;
    RemainingEnergyJ=EnergyJ;SourceAge=Age;bPowerEnabled=Enabled;PowerAccumulator=0;
    bReleased=false;if(Released)ReleaseSupport();else if(Constraint){if(Primitive.IsValid())Primitive->SetSimulatePhysics(true);Constraint->InitComponentConstraint();}
    LastImpacts.Reset();
}
void UReactiveMechanismComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);if(!GetOwner()->HasAuthority())return;
    if(!bReleased&&!Supports.IsEmpty())
    {
        int32 Broken=0;for(const auto& S:Supports)if(!IsValid(S)||S->State.bBroken)++Broken;
        if(bReleaseWhenAnySupportBreaks?Broken>0:Broken==Supports.Num())ReleaseSupport();
    }
    if(Constraint&&!bReleased)Constraint->SetAngularOrientationTarget(FRotator(0,bGateOpen?75:0,0));
    if(bBuoyant&&Primitive.IsValid()&&Primitive->IsSimulatingPhysics())
    {
        if(auto* W=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>())if(const auto* Sim=W->GetSimulation())
        for(auto Id:Sim->Query(GetOwner()->GetActorLocation(),100))
        {
            auto* Other=W->GetBodyOwner(Id);auto* Body=Other?Other->FindComponentByClass<UReactiveBodyComponent>():nullptr;
            if(!Body||!Body->bIceControlsPawnCollision||Body->State.IceFraction>=.95||Body->State.WaterKg<.2)continue;
            auto* P=Body->GetPrimitive();if(!P||!Primitive->Bounds.GetBox().Intersect(P->Bounds.GetBox()))continue;
            const float Depth=FMath::Clamp(float((P->Bounds.GetBox().Max.Z-Primitive->Bounds.GetBox().Min.Z)/FMath::Max(1.,Primitive->Bounds.BoxExtent.Z*2)),0.f,1.f);
            Primitive->AddForce(FVector(0,0,Primitive->GetMass()*980*Depth*1.8)-Primitive->GetPhysicsLinearVelocity()*Primitive->GetMass()*Depth*2);break;
        }
    }
    if(!bPowerEnabled||PowerW<=0||RemainingEnergyJ<=0)return;
    if(LifetimeSeconds>0&&SourceAge>=LifetimeSeconds){bPowerEnabled=false;return;}
    // Clamp catch-up; never emit a huge pulse after a stalled rendering frame.
    const double Step=FMath::Min(double(Dt),.1);
    const double Duration=LifetimeSeconds>0?FMath::Min(Step,LifetimeSeconds-SourceAge):Step;
    if(auto* B=GetOwner()->FindComponentByClass<UReactiveBodyComponent>())
    {
        FReactiveStimulus S;S.SourceActor=GetOwner();S.InputId=++PulseSequence;S.ElectricalJ=FMath::Min(RemainingEnergyJ,PowerW*Duration);
        if(S.ElectricalJ>0&&B->Inject(S)){RemainingEnergyJ-=S.ElectricalJ;SourceAge+=Duration;}
    }
}
void UReactiveMechanismComponent::Hit(UPrimitiveComponent* HitComponent,AActor* Other,UPrimitiveComponent*,FVector NormalImpulse,const FHitResult&)
{
    if(!GetOwner()->HasAuthority()||!bImpactDamage||!IsValid(Other)||Other==GetOwner()||!HitComponent->IsSimulatingPhysics())return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(const double* Last=LastImpacts.Find(Other);Last&&Now-*Last<.5)return;
    // Collision impulse -> lost kinetic energy proxy, SI units. One damage path only.
    const double Mass=FMath::Max(1.,double(HitComponent->GetMass()));
    const double Joules=NormalImpulse.SizeSquared()/10000./(2*Mass);
    if(Joules<ImpactThresholdJ)return;
    LastImpacts.Add(Other,Now);
    for(auto It=LastImpacts.CreateIterator();It;++It)if(!It.Key().IsValid()||Now-It.Value()>2)It.RemoveCurrent();
    AActor* Instigator=GetOwner()->GetInstigator();if(!Instigator)if(auto* B=GetOwner()->FindComponentByClass<UReactiveBodyComponent>())Instigator=B->GetLastSourceActor();if(!Instigator)Instigator=GetOwner();
    auto* Pawn=Cast<APawn>(Instigator);AController* Controller=Pawn?Pawn->GetController():Instigator->GetInstigatorController();
    UGameplayStatics::ApplyDamage(Other,FMath::Clamp(float((Joules-ImpactThresholdJ)/35),0.f,70.f),Controller,GetOwner(),nullptr);
    if(auto* Body=Other->FindComponentByClass<UReactiveBodyComponent>())
    {FReactiveStimulus S;S.SourceActor=Instigator;S.bApplyPhysicsImpulse=false;S.ImpulseNs=NormalImpulse/100.;Body->Inject(S);}
}
