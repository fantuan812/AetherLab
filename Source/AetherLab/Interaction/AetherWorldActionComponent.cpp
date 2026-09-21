#include "Interaction/AetherWorldActionComponent.h"
#include "AetherFrontier.h"
#include "Inventory/AetherResourceGate.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
UAetherWorldActionComponent::UAetherWorldActionComponent()
{PrimaryComponentTick.bCanEverTick=true;SetIsReplicatedByDefault(true);}
void UAetherWorldActionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(UAetherWorldActionComponent,Phase);DOREPLIFETIME(UAetherWorldActionComponent,Pending);DOREPLIFETIME(UAetherWorldActionComponent,bCommitted);}
AActor* UAetherWorldActionComponent::ContactActor() const
{
    if(bCommitted&&(Phase==EAetherWorldActionPhase::PutDown||Phase==EAetherWorldActionPhase::Throw))return nullptr;
    if(Pending)return Pending;
    const auto* C=Cast<AAetherFrontierCharacter>(GetOwner());return C?C->Carried.Get():nullptr;
}
bool UAetherWorldActionComponent::Reachable(AAetherFrontierCharacter& C,AAetherFrontierProp& P) const
{
    if(P.IsActorBeingDestroyed()||!P.bCarryable||P.Reactive->State.bBroken||!P.Mesh->IsSimulatingPhysics()||P.Mesh->GetMass()>80||
       (P.Carrier&&P.Carrier!=&C)||FVector::DistSquared(C.GetActorLocation(),P.GetActorLocation())>FMath::Square(200.))return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherHandReach),false,&C);Q.AddIgnoredActor(&P);
    return !GetWorld()->LineTraceTestByChannel(C.GetActorLocation()+FVector(0,0,20),P.GetActorLocation(),ECC_Visibility,Q);
}
bool UAetherWorldActionComponent::Begin(FName Action)
{
    auto* C=Cast<AAetherFrontierCharacter>(GetOwner());
    if(!C||!C->HasAuthority()||IsBusy()||!C->Alive()||C->bTravelPending||C->ResourceGate->IsBlocked()||
       C->CombatTime()<C->StunUntil||C->ReviveTarget)return false;
    if(C->Carried)
    {
        Pending=C->Carried;Phase=Action==TEXT("Throw")?EAetherWorldActionPhase::Throw:EAetherWorldActionPhase::PutDown;
        C->PresentAction(Phase==EAetherWorldActionPhase::Throw?TEXT("Throw"):TEXT("PutDown"),.65f);
    }
    else
    {
        if(Action==TEXT("Throw")||!C->Ready())return false;
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherPickup),false,C);
        const FVector From=C->GetActorLocation()+FVector(0,0,25);
        if(!GetWorld()->LineTraceSingleByChannel(Hit,From,From+C->GetControlRotation().Vector()*200,ECC_Visibility,Q))return false;
        auto* P=Cast<AAetherFrontierProp>(Hit.GetActor());if(!P||P->Carrier||!Reachable(*C,*P))return false;
        if(Action==TEXT("Push")){P->Mesh->AddImpulse(C->GetActorForwardVector()*15000);P->Mechanism->RecordImpactSource(C);C->PresentAction(TEXT("Throw"),.65f);return true;}
        // 前摇期间只占用目标，物理抓取在接触提交点执行；第二位玩家无法抢同一物体。
        P->Carrier=C;Pending=P;Phase=EAetherWorldActionPhase::Pickup;C->PresentAction(TEXT("Pickup"),.7f);
    }
    StartedAt=C->CombatTime();DamageSerial=C->DamageReceivedCount;bCommitted=false;
    C->SetSprintInput(false);C->GetCharacterMovement()->StopMovementImmediately();C->ForceNetUpdate();return true;
}
void UAetherWorldActionComponent::Cancel()
{
    auto* C=Cast<AAetherFrontierCharacter>(GetOwner());
    if(C&&Pending&&Pending->Carrier==C&&C->Carried!=Pending){Pending->Carrier=nullptr;Pending->ForceNetUpdate();}
    if(C&&(C->PresentedAction.Id==TEXT("Pickup")||C->PresentedAction.Id==TEXT("Throw")||C->PresentedAction.Id==TEXT("PutDown")))C->PresentedAction.Duration=0;
    Pending=nullptr;Phase=EAetherWorldActionPhase::Idle;bCommitted=false;
}
void UAetherWorldActionComponent::Release()
{
    auto* C=Cast<AAetherFrontierCharacter>(GetOwner());if(!C||!C->HasAuthority())return;
    Cancel();C->CarryHandle->ReleaseComponent();
    if(C->Carried)
    {
        auto* P=C->Carried.Get();P->Mechanism->RecordImpactSource(C);P->Carrier=nullptr;
        P->Mesh->IgnoreActorWhenMoving(C,false);C->GetCapsuleComponent()->IgnoreActorWhenMoving(P,false);P->ForceNetUpdate();
    }
    C->Carried=nullptr;
}
void UAetherWorldActionComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);auto* C=Cast<AAetherFrontierCharacter>(GetOwner());if(!C||!C->HasAuthority())return;
    if(!C->Alive()||C->bTravelPending||C->CombatTime()<C->StunUntil||C->ResourceGate->IsBlocked()||
       ((IsBusy()||C->Carried)&&C->DamageReceivedCount!=DamageSerial)){Release();return;}
    if(IsBusy())
    {
        if(!Pending||(!bCommitted&&!Reachable(*C,*Pending))){Release();return;}
        const float Elapsed=C->CombatTime()-StartedAt;
        if(!bCommitted&&Elapsed>=.35f)
        {
            bCommitted=true;auto* P=Pending.Get();
            if(Phase==EAetherWorldActionPhase::Pickup)
            {
                C->Carried=P;P->Mesh->IgnoreActorWhenMoving(C,true);C->GetCapsuleComponent()->IgnoreActorWhenMoving(P,true);
                C->CarryHandle->GrabComponentAtLocationWithRotation(P->Mesh,NAME_None,P->GetActorLocation(),P->GetActorRotation());
                P->Mechanism->RecordImpactSource(C);P->ForceNetUpdate();
            }
            else
            {
                const bool Throw=Phase==EAetherWorldActionPhase::Throw;
                // 提交后已释放的物体不再归动作持有；受击取消不能撤回已经施加的冲量。
                C->CarryHandle->ReleaseComponent();P->Carrier=nullptr;
                P->Mesh->IgnoreActorWhenMoving(C,false);C->GetCapsuleComponent()->IgnoreActorWhenMoving(P,false);C->Carried=nullptr;
                if(Throw)P->Mesh->AddImpulse(C->GetControlRotation().Vector()*P->Mesh->GetMass()*500);
                P->Mechanism->RecordImpactSource(C);P->ForceNetUpdate();
            }
        }
        if(Elapsed>=.7f){Pending=nullptr;Phase=EAetherWorldActionPhase::Idle;bCommitted=false;C->ForceNetUpdate();}
    }
    if(C->Carried)
    {
        auto* P=C->Carried.Get();
        if(P->Reactive->State.bBroken||!P->Mesh->IsSimulatingPhysics()||FVector::DistSquared(C->GetActorLocation(),P->GetActorLocation())>FMath::Square(250.))
        {Release();return;}
        const FVector From=C->GetActorLocation()+FVector(0,0,10);
        FVector Target=From+C->GetActorForwardVector()*85;
        FHitResult Block;FCollisionQueryParams Q(SCENE_QUERY_STAT(CarrySweep),false,C);Q.AddIgnoredActor(P);
        if(GetWorld()->SweepSingleByChannel(Block,From,Target,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(35),Q))Target=Block.Location;
        C->CarryHandle->SetTargetLocationAndRotation(Target,C->GetActorRotation());
    }
}
void UAetherWorldActionComponent::EndPlay(const EEndPlayReason::Type Reason){Release();Super::EndPlay(Reason);}
