#include "AetherTraversal.h"
#include "ReactiveMechanismComponent.h"
#include "ReactiveBodyComponent.h"
#include "Navigation/NavLinkProxy.h"
#include "NavLinkCustomComponent.h"
#include "NavAreas/NavArea_Default.h"
#include "NavAreas/NavArea_Null.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
UAetherTraversalComponent::UAetherTraversalComponent()
{PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.1f;PrimaryComponentTick.TickGroup=TG_PostPhysics;SetIsReplicatedByDefault(true);}
void UAetherTraversalComponent::BeginPlay()
{
 Super::BeginPlay();LastPose=GetOwner()->GetActorTransform();
 if(!bAuthoredBridge)return;
 if(auto* P=Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))P->SetCanEverAffectNavigation(false);
 if(GetOwner()->HasAuthority())
 {
  Link=GetWorld()->SpawnActor<ANavLinkProxy>();Link->PointLinks.Reset();
  Link->GetSmartLinkComp()->SetEnabledArea(UNavArea_Default::StaticClass());
  Link->GetSmartLinkComp()->SetDisabledArea(UNavArea_Null::StaticClass());
  Link->bSmartLinkIsRelevant=true;Link->SetSmartLinkEnabled(false);
 }
}
void UAetherTraversalComponent::SetOpen(bool Open)
{
 if(Open==bRouteOpen)return;bRouteOpen=Open;++Revision;
 if(auto* P=Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))P->SetCanEverAffectNavigation(Open);
 if(Link)Link->SetSmartLinkEnabled(Open);GetOwner()->ForceNetUpdate();
}
void UAetherTraversalComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
 Super::TickComponent(Dt,Type,Tick);if(!bAuthoredBridge||!GetOwner()->HasAuthority())return;
 auto* P=Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());auto* M=GetOwner()->FindComponentByClass<UReactiveMechanismComponent>();auto* B=GetOwner()->FindComponentByClass<UReactiveBodyComponent>();
 const auto Pose=GetOwner()->GetActorTransform();
 const FVector A=Pose.TransformPosition(StartLocal),Z=Pose.TransformPosition(EndLocal);
 bool Safe=P&&M&&M->bReleased&&B&&!B->State.bBroken&&P->IsCollisionEnabled()&&P->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block
   &&GetOwner()->GetActorUpVector().Z>.94&&P->GetComponentVelocity().Size()<=MaxSpeedCm&&(!P->IsSimulatingPhysics()||P->GetPhysicsAngularVelocityInDegrees().Size()<5)
   &&Pose.GetLocation().Equals(LastPose.GetLocation(),1)&&Pose.GetRotation().AngularDistance(LastPose.GetRotation())<.01;
 FCollisionQueryParams Q(SCENE_QUERY_STAT(BridgeBanks),false,GetOwner());
 for(const FVector End:{A,Z})
 {FHitResult Hit;Safe=Safe&&GetWorld()->LineTraceSingleByChannel(Hit,End+FVector(0,0,30),End-FVector(0,0,80),ECC_Visibility,Q)&&Hit.ImpactNormal.Z>.7&&Hit.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block;}
 StableSeconds=Safe?StableSeconds+FMath::Min(Dt,.15f):0;LastPose=Pose;
 const bool Open=Safe&&StableSeconds>=FMath::Max(.1f,SettleSeconds);
 if(Link&&Open&&!bRouteOpen){Link->SetActorLocation(Pose.GetLocation());Link->GetSmartLinkComp()->SetLinkData(A-Pose.GetLocation(),Z-Pose.GetLocation(),ENavLinkDirection::BothWays);}
 SetOpen(Open);
}
void UAetherTraversalComponent::EndPlay(const EEndPlayReason::Type Reason)
{SetOpen(false);if(Link)Link->Destroy();Super::EndPlay(Reason);}
void UAetherTraversalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(UAetherTraversalComponent,bRouteOpen);DOREPLIFETIME(UAetherTraversalComponent,Revision);}
