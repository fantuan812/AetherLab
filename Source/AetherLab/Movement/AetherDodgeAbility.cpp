#include "Movement/AetherDodgeAbility.h"
#include "AetherFrontier.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotionConstantForce.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NativeGameplayTags.h"

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
    Cost.ModifierOp=EGameplayModOp::Additive;Cost.ModifierMagnitude=FScalableFloat(-18.f);Modifiers.Add(Cost);
}
UAetherDodgeCooldown::UAetherDodgeCooldown()
{
    DurationPolicy=EGameplayEffectDurationType::HasDuration;DurationMagnitude=FScalableFloat(.7f);
    // 效果 CDO 构造阶段必须创建具名默认子对象，不能调用运行时 NewObject 工厂。
    auto* Tags=CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GrantedTags"));
    GEComponents.Add(Tags);GrantTag(*Tags,TAG_DodgeCooldown);
}
UAetherDodgeInvulnerability::UAetherDodgeInvulnerability()
{
    DurationPolicy=EGameplayEffectDurationType::HasDuration;DurationMagnitude=FScalableFloat(.22f);
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
    if(!C||C->AbilitySystem!=Info->AbilitySystemComponent.Get()||!C->Ready()||C->IsCrouched()||
        !C->GetCharacterMovement()->IsMovingOnGround())return false;
    if(const auto* Player=Cast<AAetherFrontierCharacter>(C))
        if(Player->bTravelPending||Player->Carried||Player->ReviveTarget||(Player->IsLocallyControlled()&&Player->bPanel))return false;
    return Super::CanActivateAbility(H,Info,Source,Target,Relevant);
}
void UAetherDodgeAbility::ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation,const FGameplayEventData*)
{
    auto* C=Info?Cast<AAetherCharacter>(Info->AvatarActor.Get()):nullptr;
    if(!C){EndAbility(H,Info,Activation,true,true);return;}
    ActiveCharacter=C;ActiveSystem=Info->AbilitySystemComponent;
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
    // 输入方向来自 CharacterMovement 已传输的加速度；没有输入时朝角色前方。
    // IgnoreZ 保留重力；不调用 LaunchCharacter，也不强制保持 Walking，越过边缘自然下落。
    FVector Direction=C->GetCharacterMovement()->GetCurrentAcceleration().GetSafeNormal2D();
    if(Direction.IsNearlyZero())Direction=C->GetActorForwardVector().GetSafeNormal2D();
    const FVector Local=C->GetActorTransform().InverseTransformVectorNoScale(Direction);
    C->PresentAction(FMath::Abs(Local.X)>=FMath::Abs(Local.Y)?(Local.X>=0?TEXT("DodgeForward"):TEXT("DodgeBack")):(Local.Y>=0?TEXT("DodgeRight"):TEXT("DodgeLeft")),.55f);
    auto* Motion=UAbilityTask_ApplyRootMotionConstantForce::ApplyRootMotionConstantForce(this,TEXT("Aether.Dodge"),
        Direction,700,.35f,false,nullptr,ERootMotionFinishVelocityMode::ClampVelocity,FVector::ZeroVector,0,true);
    Motion->ReadyForActivation();
    auto* Recovery=UAbilityTask_WaitDelay::WaitDelay(this,.55f);
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
    if(ActiveSystem.IsValid()&&Invulnerability.IsValid())ActiveSystem->RemoveActiveGameplayEffect(Invulnerability);
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
