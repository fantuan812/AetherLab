#include "Interaction/AetherWorldActionComponent.h"
#include "Framework/AetherFrontier.h"
#include "Inventory/AetherResourceGate.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Combat/AetherActionTiming.h"
#include "Combat/AetherControlledActionDefinition.h"
UAetherWorldActionComponent::UAetherWorldActionComponent()
{PrimaryComponentTick.bCanEverTick=true;SetIsReplicatedByDefault(true);}
void UAetherWorldActionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(UAetherWorldActionComponent,Phase);DOREPLIFETIME(UAetherWorldActionComponent,Pending);DOREPLIFETIME(UAetherWorldActionComponent,bCommitted);}
AActor* UAetherWorldActionComponent::ContactActor() const
{
    if(bCommitted&&(Phase==EAetherWorldActionPhase::PutDown||Phase==EAetherWorldActionPhase::Throw||Phase==EAetherWorldActionPhase::Push))return nullptr;
    if(Pending)return Pending;
    const auto* C=Cast<AAetherFrontierCharacter>(GetOwner());return C?C->Carried.Get():nullptr;
}
bool UAetherWorldActionComponent::Reachable(AAetherFrontierCharacter& C,AAetherFrontierProp& P) const
{
    return ManipulationReason(C,P).IsEmpty();
}
FString UAetherWorldActionComponent::ManipulationReason(const AAetherFrontierCharacter& C,const AAetherFrontierProp& P)
{
    if(!P.bEnabled||P.IsActorBeingDestroyed()||!P.bCarryable)return TEXT("此物件不能搬运或推动");
    if(P.Reactive->State.bBroken)return TEXT("物件已损坏，无法搬运");
    if(!P.Mesh->IsSimulatingPhysics())return TEXT("物件当前固定，无法搬运");
    if(P.Mesh->GetMass()>80)return TEXT("物件过重，无法搬运或推动");
    if(P.Carrier&&P.Carrier!=&C)return TEXT("物件正被其他角色占用");
    if(FVector::DistSquared(C.GetActorLocation(),P.GetActorLocation())>FMath::Square(200.))return TEXT("靠近物件后可搬运");
    FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherHandReach),false,&C);Q.AddIgnoredActor(&P);
    if(C.GetWorld()->LineTraceTestByChannel(C.GetActorLocation()+FVector(0,0,20),P.GetActorLocation(),ECC_Visibility,Q))return TEXT("物件被障碍挡住");
    return {};
}
bool UAetherWorldActionComponent::Begin(FName Action,AAetherFrontierProp* SelectedTarget)
{
    auto* C=Cast<AAetherFrontierCharacter>(GetOwner());
    if(!C||!C->HasAuthority()||IsBusy()||!C->Alive()||C->bTravelPending||C->ResourceGate->IsBlocked()||
       C->CombatTime()<C->StunUntil||C->ReviveTarget||(Action!=TEXT("Push")&&Action!=TEXT("Carry")&&Action!=TEXT("Throw")))return false;
    if(C->Carried)
    {
        if(Action==TEXT("Push")||SelectedTarget!=C->Carried)return false;
        Pending=C->Carried;Phase=Action==TEXT("Throw")?EAetherWorldActionPhase::Throw:EAetherWorldActionPhase::PutDown;
        C->PresentAction(Phase==EAetherWorldActionPhase::Throw?TEXT("Throw"):TEXT("PutDown"),AetherActionTiming::ReleaseDuration);
    }
    else
    {
        if(Action==TEXT("Throw")||!C->Ready())return false;
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherPickup),false,C);
        const FVector From=C->GetActorLocation()+FVector(0,0,25);
        if(!GetWorld()->LineTraceSingleByChannel(Hit,From,From+C->GetControlRotation().Vector()*200,ECC_Visibility,Q))return false;
        auto* P=Cast<AAetherFrontierProp>(Hit.GetActor());if(!P||P!=SelectedTarget||P->Carrier||!Reachable(*C,*P))return false;
        // 前摇期间只占用目标，物理抓取在接触提交点执行；第二位玩家无法抢同一物体。
        P->Carrier=C;Pending=P;Phase=Action==TEXT("Push")?EAetherWorldActionPhase::Push:EAetherWorldActionPhase::Pickup;
        C->PresentAction(Action==TEXT("Push")?TEXT("Push"):TEXT("Pickup"),Action==TEXT("Push")?AetherActionTiming::PushDuration:AetherActionTiming::PickupDuration);
    }
    StartedAt=C->CombatTime();DamageSerial=C->CombatRuntime->DamageReceivedCount;bCommitted=false;
    const auto* Definition=AetherControlledActions::Find(C->PresentedAction.Id);
    if(!Definition||!Definition->IsValid()){Cancel();return false;}
    CommitAt=StartedAt+Definition->CommitTime;EndsAt=StartedAt+Definition->Duration;
    CommittedDirection=(Phase==EAetherWorldActionPhase::Push?C->GetActorForwardVector():C->GetControlRotation().Vector()).GetSafeNormal();
    ++ActionSerial;UE_LOG(LogTemp,Verbose,TEXT("AETHER_ACTION_STARTED serial=%u action=%s"),ActionSerial,*Action.ToString());
    C->SetSprintInput(false);C->GetCharacterMovement()->StopMovementImmediately();C->ForceNetUpdate();return true;
}
void UAetherWorldActionComponent::Cancel()
{
    auto* C=Cast<AAetherFrontierCharacter>(GetOwner());
    if(C&&Pending&&Pending->Carrier==C&&C->Carried!=Pending){Pending->Carrier=nullptr;Pending->ForceNetUpdate();}
    if(C&&(C->PresentedAction.Id==TEXT("Pickup")||C->PresentedAction.Id==TEXT("Throw")||C->PresentedAction.Id==TEXT("PutDown")||C->PresentedAction.Id==TEXT("Push")))C->PresentedAction.Duration=0;
    if(IsBusy())UE_LOG(LogTemp,Verbose,TEXT("AETHER_ACTION_ENDED serial=%u committed=%d"),ActionSerial,bCommitted);
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
       ((IsBusy()||C->Carried)&&C->CombatRuntime->DamageReceivedCount!=DamageSerial)){Release();return;}
    if(IsBusy())
    {
        if(!Pending||(!bCommitted&&!Reachable(*C,*Pending))){Release();return;}
        if(!bCommitted&&C->CombatTime()>=CommitAt)
        {
            bCommitted=true;auto* P=Pending.Get();
            UE_LOG(LogTemp,Verbose,TEXT("AETHER_ACTION_COMMITTED serial=%u"),ActionSerial);
            if(Phase==EAetherWorldActionPhase::Push)
            {
                P->Mesh->AddImpulse(CommittedDirection*AetherActionTiming::PushImpulse);
                P->Mechanism->RecordImpactSource(C);P->Carrier=nullptr;P->ForceNetUpdate();
            }
            else if(Phase==EAetherWorldActionPhase::Pickup)
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
                if(Throw)P->Mesh->AddImpulse(CommittedDirection*P->Mesh->GetMass()*500);
                P->Mechanism->RecordImpactSource(C);P->ForceNetUpdate();
            }
        }
        if(C->CombatTime()>=EndsAt){Cancel();C->ForceNetUpdate();}
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
void AAetherFrontierCharacter::RequestWorldAction(FName Action)
{
    if(bPanel||bTravelPending||!Alive()||WorldActionSequence==MAX_uint32)return;
    AAetherFrontierProp* Selected=Carried.Get();
    if(!Selected)
    {
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherSelectHandTarget),false,this);
        const FVector From=GetActorLocation()+FVector(0,0,25);
        if(GetWorld()->LineTraceSingleByChannel(Hit,From,From+GetControlRotation().Vector()*200,ECC_Visibility,Q))Selected=Cast<AAetherFrontierProp>(Hit.GetActor());
    }
    if(Selected)ServerSelectedWorldAction(Action,Selected,++WorldActionSequence);
}
void AAetherFrontierCharacter::ServerSelectedWorldAction_Implementation(FName Action,AAetherFrontierProp* Target,uint32 Sequence)
{
    if(!Sequence||Sequence<=LastWorldActionSequence)return;LastWorldActionSequence=Sequence;
    if(!IsValid(Target)||CombatTime()<NextServerAction)return;NextServerAction=CombatTime()+.12f;
    if(!WorldActions->Begin(Action,Target))Notify(TEXT("物体不可达、被占用或当前无法执行动作。"));
}
