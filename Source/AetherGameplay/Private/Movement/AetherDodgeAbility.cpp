#include "Movement/AetherDodgeAbility.h"
#include "Framework/AetherFrontier.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotionConstantForce.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NativeGameplayTags.h"
#include "Combat/AetherControlledActionDefinition.h"
#include "AbilitySystemComponent.h"
#include "GameplayPrediction.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_DodgeActive,"Aether.Action.Dodge");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_DodgeCooldown,"Aether.Cooldown.Dodge");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_DodgeInvulnerable,"Aether.State.DodgeInvulnerable");
namespace AetherDodge
{
FGameplayTag ActiveTag(){return TAG_DodgeActive;}
FGameplayTag InvulnerableTag(){return TAG_DodgeInvulnerable;}
}
namespace
{
void GrantTag(UTargetTagsGameplayEffectComponent& Component,FGameplayTag Tag)
{
    FInheritedTagContainer Tags;Tags.AddTag(Tag);
    Component.SetAndApplyTargetTagChanges(Tags);
}
}
UAetherDodgeCost::UAetherDodgeCost()
{
    DurationPolicy=EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo Cost;Cost.Attribute=UAetherAttributes::GetStaminaAttribute();
    const auto* Rule=AetherControlledActions::Find(TEXT("DodgeForward"));
    Cost.ModifierOp=EGameplayModOp::Additive;Cost.ModifierMagnitude=FScalableFloat(Rule?-Rule->Cost:0);Modifiers.Add(Cost);
}
UAetherDodgeCooldown::UAetherDodgeCooldown()
{
    const auto* Rule=AetherControlledActions::Find(TEXT("DodgeForward"));
    DurationPolicy=EGameplayEffectDurationType::HasDuration;DurationMagnitude=FScalableFloat(Rule?Rule->Cooldown:0);
    // 效果 CDO 构造阶段必须创建具名默认子对象，不能调用运行时 NewObject 工厂。
    auto* Tags=CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GrantedTags"));
    GEComponents.Add(Tags);GrantTag(*Tags,TAG_DodgeCooldown);
}
UAetherDodgeInvulnerability::UAetherDodgeInvulnerability()
{
    const auto* Rule=AetherControlledActions::Find(TEXT("DodgeForward"));
    DurationPolicy=EGameplayEffectDurationType::HasDuration;DurationMagnitude=FScalableFloat(Rule?Rule->InvulnerabilityTime:0);
    // 效果 CDO 构造阶段必须创建具名默认子对象，不能调用运行时 NewObject 工厂。
    auto* Tags=CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GrantedTags"));
    GEComponents.Add(Tags);GrantTag(*Tags,TAG_DodgeInvulnerable);
}
UAetherDodgeAbility::UAetherDodgeAbility()
{
    NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
    bRetriggerInstancedAbility=false;
    CostGameplayEffectClass=UAetherDodgeCost::StaticClass();CooldownGameplayEffectClass=UAetherDodgeCooldown::StaticClass();
    ActivationOwnedTags.AddTag(TAG_DodgeActive);
}
bool UAetherDodgeAbility::CanActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,
    const FGameplayTagContainer* Source,const FGameplayTagContainer* Target,FGameplayTagContainer* Relevant) const
{
    const auto* C=Info?Cast<AAetherCharacter>(Info->AvatarActor.Get()):nullptr;
    if(!CanCommitDodge(C,Info,false))return false;
    return Super::CanActivateAbility(H,Info,Source,Target,Relevant);
}
bool UAetherDodgeAbility::CanCommitDodge(const AAetherCharacter* C,const FGameplayAbilityActorInfo* Info,bool bOwnDodgeActive) const
{
    const auto* Rule=AetherControlledActions::Find(TEXT("DodgeForward"));
    if(!Rule||!Rule->IsValid()||Rule->CancelPolicy!=EAetherActionCancelPolicy::AbilityOwned||
        Rule->ContactPolicy!=EAetherActionContactPolicy::Ground)return false;
    if(!C||!Info||!C->AbilitySystem||C->AbilitySystem!=Info->AbilitySystemComponent.Get()||
        Info->AvatarActor.Get()!=C||C->AbilitySystem->GetAvatarActor()!=C||C->QueryAction(EAetherActionKind::Dodge,true)!=EAetherActionDenial::None||
        !(Rule->AllowedStances&(C->IsCrouched()?2:1))||!C->GetCharacterMovement()||!C->GetCharacterMovement()->IsMovingOnGround())return false;
    if(bOwnDodgeActive)
    {
        if(!IsActive()||ActiveCharacter.Get()!=C||ActiveSystem.Get()!=C->AbilitySystem||
            C->PresentedAction.Serial!=ActivationActionSerial)return false;
    }
    else if(C->AbilitySystem->HasMatchingGameplayTag(TAG_DodgeActive))return false;
    if(const auto* Player=Cast<AAetherFrontierCharacter>(C))
        if(Player->bTravelPending||Player->Carried||Player->ReviveTarget||(Player->IsLocallyControlled()&&Player->bPanel))return false;
    return true;
}
void UAetherDodgeAbility::ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation,const FGameplayEventData*)
{
    auto* C=Info?Cast<AAetherCharacter>(Info->AvatarActor.Get()):nullptr;
    if(!C){EndAbility(H,Info,Activation,true,true);return;}
    ActiveCharacter=C;ActiveSystem=Info->AbilitySystemComponent;bMotionStarted=false;bAwaitingDirection=false;
    ActivationActionSerial=C->PresentedAction.Serial;
    if(!ActiveSystem.IsValid()){EndAbility(H,Info,Activation,true,true);return;}
    if(C->HasAuthority()&&C->IsPlayerControlled()&&!Info->IsLocallyControlled())
    {
        bAwaitingDirection=true;
        DirectionDelegate=ActiveSystem->AbilityTargetDataSetDelegate(H,Activation.GetActivationPredictionKey()).AddUObject(this,&UAetherDodgeAbility::ReceiveDirection);
        if(!ActiveSystem->CallReplicatedTargetDataDelegatesIfSet(H,Activation.GetActivationPredictionKey()))
        {
            auto* Timeout=UAbilityTask_WaitDelay::WaitDelay(this,.75f);
            Timeout->OnFinish.AddDynamic(this,&UAetherDodgeAbility::DirectionTimeout);Timeout->ReadyForActivation();
        }
        return;
    }
    FVector Direction=C->GetCharacterMovement()->GetCurrentAcceleration().GetSafeNormal2D();
    if(Direction.IsNearlyZero())Direction=C->GetActorForwardVector().GetSafeNormal2D();
    if(!C->HasAuthority())
    {
        FScopedPredictionWindow Prediction(ActiveSystem.Get(),true);
        auto* Target=new FAetherDodgeDirection();Target->Direction=Direction;
        FGameplayAbilityTargetDataHandle Data(Target);
        ActiveSystem->CallServerSetReplicatedTargetData(H,Activation.GetActivationPredictionKey(),Data,FGameplayTag(),ActiveSystem->ScopedPredictionKey);
    }
    StartMotion(Direction);
}
void UAetherDodgeAbility::ReceiveDirection(const FGameplayAbilityTargetDataHandle& Data,FGameplayTag)
{
    if(!IsActive()||!ActiveSystem.IsValid()||bMotionStarted||!bAwaitingDirection)return;
    bAwaitingDirection=false;
    const bool Valid=Data.Num()==1&&Data.Get(0)&&Data.Get(0)->GetScriptStruct()==FAetherDodgeDirection::StaticStruct();
    const FVector Direction=Valid?FVector(static_cast<const FAetherDodgeDirection*>(Data.Get(0))->Direction):FVector::ZeroVector;
    ActiveSystem->ConsumeClientReplicatedTargetData(CurrentSpecHandle,CurrentActivationInfo.GetActivationPredictionKey());
    if(!Valid||Direction.ContainsNaN()||FMath::Abs(Direction.Z)>.01f||!FMath::IsNearlyEqual(Direction.SizeSquared2D(),1.f,.02f))
    {EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);return;}
    StartMotion(Direction.GetSafeNormal2D());
}
void UAetherDodgeAbility::DirectionTimeout()
{if(bAwaitingDirection&&!bMotionStarted)EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);}
void UAetherDodgeAbility::StartMotion(const FVector& Direction)
{
    auto* C=ActiveCharacter.Get();const auto H=CurrentSpecHandle;const auto* Info=CurrentActorInfo;const auto Activation=CurrentActivationInfo;
    if(bMotionStarted||!CanCommitDodge(C,Info,true))
    {EndAbility(H,Info,Activation,true,true);return;}
    const FVector Local=C->GetActorTransform().InverseTransformVectorNoScale(Direction);
    const FName Action=FMath::Abs(Local.X)>=FMath::Abs(Local.Y)?(Local.X>=0?TEXT("DodgeForward"):TEXT("DodgeBack")):(Local.Y>=0?TEXT("DodgeRight"):TEXT("DodgeLeft"));
    const auto* Rule=AetherControlledActions::Find(Action);
    if(!Rule||!Rule->IsValid()){EndAbility(H,Info,Activation,true,true);return;}
    bMotionStarted=true;
    // 提交只发生一次，预测失败由 GAS 回滚成本。服务器拒绝时能力取消同时移除根运动。
    if(!CommitAbility(H,Info,Activation)||!IsActive()||!ActiveCharacter.IsValid()||
        !ActiveSystem.IsValid()||ActiveSystem->GetAvatarActor()!=C)
    {if(IsActive())EndAbility(H,Info,Activation,true,true);return;}
    if(C->HasAuthority())
    {
        C->RecordDodgeCommit();
        Invulnerability=ApplyGameplayEffectToOwner(H,Info,Activation,GetDefault<UAetherDodgeInvulnerability>(),1);
    }
    if(auto* Player=Cast<AAetherFrontierCharacter>(C))Player->SetSprintInput(false);
    // 本地预测与服务器使用相同的单次输入方向，不能等待下一帧加速度改变动作方向。
    // IgnoreZ 保留重力；不调用 LaunchCharacter，也不强制保持 Walking，越过边缘自然下落。
    C->PresentAction(Action,Rule->Duration);
    auto* Motion=UAbilityTask_ApplyRootMotionConstantForce::ApplyRootMotionConstantForce(this,TEXT("Aether.Dodge"),
        Direction,Rule->MotionSpeed,Rule->MotionTime,false,nullptr,ERootMotionFinishVelocityMode::ClampVelocity,FVector::ZeroVector,0,true);
    Motion->ReadyForActivation();
    auto* Recovery=UAbilityTask_WaitDelay::WaitDelay(this,Rule->Duration);
    Recovery->OnFinish.AddDynamic(this,&UAetherDodgeAbility::FinishRecovery);Recovery->ReadyForActivation();
}
void UAetherDodgeAbility::FinishRecovery()
{EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,false);}
void UAetherDodgeAbility::EndAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation,bool Replicate,bool Cancelled)
{
    if(!IsEndAbilityValid(H,Info))return;
    if(ScopeLockCount>0)
    {WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this,&UAetherDodgeAbility::EndAbility,H,Info,Activation,Replicate,Cancelled));return;}
    // 使用激活时的 ASC，避免换 Pawn 后的迟到取消触碰新角色。取消不返还已提交成本或冷却。
    if(ActiveSystem.IsValid())
    {
        if(Invulnerability.IsValid())ActiveSystem->RemoveActiveGameplayEffect(Invulnerability);
        ActiveSystem->AbilityTargetDataSetDelegate(H,Activation.GetActivationPredictionKey()).Remove(DirectionDelegate);
        ActiveSystem->ConsumeClientReplicatedTargetData(H,Activation.GetActivationPredictionKey());
    }
    DirectionDelegate.Reset();bMotionStarted=false;bAwaitingDirection=false;ActivationActionSerial=0;
    if(auto* C=ActiveCharacter.Get();C&&C->PresentedAction.Id.ToString().StartsWith(TEXT("Dodge")))C->PresentedAction.Duration=0;
    Invulnerability.Invalidate();ActiveCharacter.Reset();ActiveSystem.Reset();
    // 基类结束所有任务；RootMotion task 的 OnDestroy 移除对应 source。
    Super::EndAbility(H,Info,Activation,Replicate,Cancelled);
}
void UAetherDodgeAbility::OnAvatarSet(const FGameplayAbilityActorInfo* Info,const FGameplayAbilitySpec& Spec)
{
    if(IsInstantiated()&&IsActive()&&ActiveCharacter.IsValid()&&(!Info||Info->AvatarActor!=ActiveCharacter))
        EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
    Super::OnAvatarSet(Info,Spec);
}
