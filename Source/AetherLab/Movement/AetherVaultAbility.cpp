#include "Movement/AetherVaultAbility.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Inventory/AetherResourceGate.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotionMoveToForce.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VaultActive,"Aether.Action.Vault");
namespace AetherVault {FGameplayTag ActiveTag(){return TAG_VaultActive;}}
UAetherVaultAbility::UAetherVaultAbility()
{
    NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
    bRetriggerInstancedAbility=false;ActivationOwnedTags.AddTag(TAG_VaultActive);
}
bool UAetherVaultAbility::FindPath(AAetherFrontierCharacter& C,TArray<FVector>& Points,FVector* Contact)
{
    Points.Reset();auto* M=C.GetCharacterMovement();auto* Capsule=C.GetCapsuleComponent();
    if(!M->IsMovingOnGround()||C.IsCrouched())return false;
    const FVector Start=C.GetActorLocation(),Forward=C.GetActorForwardVector().GetSafeNormal2D();
    const float Half=Capsule->GetScaledCapsuleHalfHeight(),Radius=Capsule->GetScaledCapsuleRadius();
    const FVector Feet=Start-FVector(0,0,Half);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherVault),false,&C);FHitResult Front,Top,Ground;
    // 超过普通台阶高度、距脚前 110cm 以内的实心障碍。活动物理物体不作为翻越支点。
    if(!C.GetWorld()->LineTraceSingleByChannel(Front,Feet+FVector(0,0,50),Feet+FVector(0,0,50)+Forward*110,ECC_Pawn,Q)||
       !Front.GetComponent()||Front.GetComponent()->Mobility!=EComponentMobility::Static||Front.ImpactNormal.Z>.3f)return false;
    const FVector TopXY=Front.ImpactPoint+Forward*12;
    if(!C.GetWorld()->LineTraceSingleByChannel(Top,FVector(TopXY.X,TopXY.Y,Feet.Z+130),FVector(TopXY.X,TopXY.Y,Feet.Z+45),ECC_Pawn,Q)||
       Top.GetComponent()!=Front.GetComponent()||Top.ImpactNormal.Z<M->GetWalkableFloorZ())return false;
    const float Height=Top.ImpactPoint.Z-Feet.Z;if(Height<50||Height>110)return false;
    // 从障碍背面探测厚度；超过 100cm 的台面交给普通路线，不把大平台误作薄障碍。
    FHitResult Back;const FVector Across=Front.ImpactPoint+Forward*110;
    if(!C.GetWorld()->LineTraceSingleByChannel(Back,Across,Front.ImpactPoint-Forward*5,ECC_Pawn,Q)||
       Back.bStartPenetrating||Back.GetComponent()!=Front.GetComponent()||FVector::Dist2D(Back.ImpactPoint,Front.ImpactPoint)>100)return false;
    const FVector LandingXY=Back.ImpactPoint+Forward*(Radius+12);
    if(!C.GetWorld()->LineTraceSingleByChannel(Ground,FVector(LandingXY.X,LandingXY.Y,Feet.Z+125),FVector(LandingXY.X,LandingXY.Y,Feet.Z-40),ECC_Pawn,Q)||
       !Ground.GetComponent()||Ground.GetComponent()->Mobility!=EComponentMobility::Static||
       Ground.ImpactNormal.Z<M->GetWalkableFloorZ()||FMath::Abs(Ground.ImpactPoint.Z-Feet.Z)>35)return false;
    const FVector Land=Ground.ImpactPoint+FVector(0,0,Half+2);
    const float ClearZ=Top.ImpactPoint.Z+Half+6;
    Points={FVector(Start.X,Start.Y,ClearZ),FVector(Land.X,Land.Y,ClearZ),Land};
    // 完整胶囊扫掠三段路径，包括升起时的头顶净空；不忽略障碍本身的碰撞。
    FVector Previous=Start;const auto Shape=FCollisionShape::MakeCapsule(Radius,Half);
    for(const FVector& Point:Points)
    {
        FHitResult Hit;
        if(C.GetWorld()->SweepSingleByChannel(Hit,Previous,Point,FQuat::Identity,ECC_Pawn,Shape,Q)||
           C.GetWorld()->OverlapBlockingTestByChannel(Point,FQuat::Identity,ECC_Pawn,Shape,Q)){Points.Reset();return false;}
        Previous=Point;
    }
    if(Contact)*Contact=Top.ImpactPoint;
    return true;
}
bool UAetherVaultAbility::CanActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,const FGameplayTagContainer* S,const FGameplayTagContainer* T,FGameplayTagContainer* R) const
{
    auto* C=Info?Cast<AAetherFrontierCharacter>(Info->AvatarActor.Get()):nullptr;TArray<FVector> Candidate;
    return C&&C->CanStartLocomotion()&&(!C->IsLocallyControlled()||!C->bPanel)&&FindPath(*C,Candidate)&&Super::CanActivateAbility(H,Info,S,T,R);
}
void UAetherVaultAbility::ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,const FGameplayEventData*)
{
    auto* C=Info?Cast<AAetherFrontierCharacter>(Info->AvatarActor.Get()):nullptr;
    FVector Contact;
    if(!C||!FindPath(*C,Path,&Contact)||!CommitAbility(H,Info,A)){EndAbility(H,Info,A,true,true);return;}
    C->PresentAction(TEXT("Vault"),.82f);C->PresentedAction.bHasContact=true;C->PresentedAction.Contact=Contact;
    Character=C;DamageAtStart=C->LastDamageAt;Phase=0;C->SetSprintInput(false);C->StopJumping();
    C->GetCharacterMovement()->StopMovementImmediately();C->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    // Flying 仅是受控 root source 的运动阶段，仍由 CharacterMovement 扫掠碰撞；结束必恢复重力。
    C->GetWorldTimerManager().SetTimer(Watch,this,&UAetherVaultAbility::CheckInterruption,.025f,true);
    NextPhase();
}
void UAetherVaultAbility::NextPhase()
{
    auto* C=Character.Get();if(!C||!IsActive()){Abort();return;}
    if(Phase>0&&FVector::DistSquared(C->GetActorLocation(),Path[Phase-1])>FMath::Square(12.f)){Abort();return;}
    if(Phase==Path.Num()){EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,false);return;}
    const float Duration=Phase==1?.38f:.22f;
    auto* Task=UAbilityTask_ApplyRootMotionMoveToForce::ApplyRootMotionMoveToForce(this,FName(*FString::Printf(TEXT("Aether.Vault.%d"),Phase)),
        Path[Phase++],Duration,false,MOVE_Flying,true,nullptr,ERootMotionFinishVelocityMode::SetVelocity,FVector::ZeroVector,0);
    Task->OnTimedOutAndDestinationReached.AddDynamic(this,&UAetherVaultAbility::NextPhase);
    Task->OnTimedOut.AddDynamic(this,&UAetherVaultAbility::Abort);Task->ReadyForActivation();
}
void UAetherVaultAbility::CheckInterruption()
{
    auto* C=Character.Get();
    if(!C||!C->Alive()||C->bTravelPending||C->ResourceGate->IsBlocked()||C->CombatTime()<C->StunUntil||
       C->LastDamageAt>DamageAtStart||!CurrentActorInfo||CurrentActorInfo->AvatarActor.Get()!=C)Abort();
}
void UAetherVaultAbility::Abort(){if(IsActive())EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);}
void UAetherVaultAbility::EndAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,bool Replicate,bool Cancelled)
{
    if(!IsEndAbilityValid(H,Info))return;
    if(ScopeLockCount>0){WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this,&UAetherVaultAbility::EndAbility,H,Info,A,Replicate,Cancelled));return;}
    auto* C=Character.Get();if(C)C->GetWorldTimerManager().ClearTimer(Watch);
    Super::EndAbility(H,Info,A,Replicate,Cancelled);
    if(C&&C->PresentedAction.Id==TEXT("Vault"))C->PresentedAction.Duration=0;
    if(C&&!C->bTravelPending){C->GetCharacterMovement()->StopMovementImmediately();C->GetCharacterMovement()->SetMovementMode(C->Alive()?MOVE_Falling:MOVE_None);}
    Character.Reset();Path.Reset();
}
void UAetherVaultAbility::OnAvatarSet(const FGameplayAbilityActorInfo* Info,const FGameplayAbilitySpec& Spec)
{
    if(IsInstantiated()&&IsActive()&&(!Info||Info->AvatarActor.Get()!=Character.Get()))Abort();
    Super::OnAvatarSet(Info,Spec);
}
