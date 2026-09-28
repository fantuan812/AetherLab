#include "Abilities/AetherSpellAbility.h"
#include "Combat/AetherCombat.h"
#include "Skills/AetherSkillDefinitions.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Inventory/AetherResourceGate.h"
#include "Framework/AetherProgression.h"
#include "Effects/AetherBuffRuntime.h"
#include "TimerManager.h"
#include "Engine/World.h"
UAetherSpellAbility::UAetherSpellAbility()
{ InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor; NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;bRetriggerInstancedAbility=false; }
namespace
{
// 每次都由权威 ASC 的 Spec 解析，绝不接受客户端传入的 Rank 或效果数值。
bool ResolveSkill(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FString& Id,int32& Rank)
{
    const auto* ASC=Info?Info->AbilitySystemComponent.Get():nullptr;
    const auto* Spec=ASC?ASC->FindAbilitySpecFromHandle(H):nullptr;
    if(!Spec)return false;
    Id=AetherSkillBinding::Identify(*Spec);Rank=Spec->Level;
    return FAetherSkillDefinitionsV10::Get().Effect(Id,Rank)!=nullptr;
}
}
float UAetherSpellAbility::Cost(int32 Spell)
{
    const auto& D=FAetherSkillDefinitionsV10::Get();const auto* S=D.Legacy(Spell);
    const auto* E=S?D.Effect(S->SkillId,1):nullptr;
    return E?float(E->ManaCost):0.f;
}
bool UAetherSpellAbility::CheckCost(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayTagContainer* Tags) const
{
    FString Id;int32 Rank=0;if(!ResolveSkill(H,Info,Id,Rank))return false;
    const auto* C=Info?Cast<AAetherCharacter>(Info->AvatarActor.Get()):nullptr;
    const auto& E=*FAetherSkillDefinitionsV10::Get().Effect(Id,Rank);
    const bool SelfEffect=FAetherSkillDefinitionsV10::Get().Skills.FindChecked(Id).Mechanic==EAetherSkillMechanic::SelfBuff;
    return C&&C->QueryAction(EAetherActionKind::Spell)==EAetherActionDenial::None&&C->SkillCooldownRemaining(Id)<=0&&
        (!C->HasAuthority()||!SelfEffect||C->BuffRuntime->CanApply(E.BuffId))&&C->SkillUnlocked(Id)&&C->Mana()>=E.ManaCost
        &&C->WaterReserveKg>=E.WaterKg&&Super::CheckCost(H,Info,Tags);
}
void UAetherSpellAbility::ApplyCost(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A) const
{
    FString Id;int32 Rank=0;if(!ResolveSkill(H,Info,Id,Rank))return;
    if(!PaymentExecution.IsValid()||PaymentExecution->bCostApplied||!PreparedCast.IsSet()||PreparedCast->SkillId!=Id||PreparedCast->Rank!=Rank||PaymentExecution->ExecutionId!=PreparedCast->ExecutionId)return;
    bPaid=true;PaymentExecution->bCostApplied=true;
    Info->AbilitySystemComponent->ApplyModToAttribute(UAetherAttributes::GetManaAttribute(),EGameplayModOp::Additive,-float(PreparedCast->Effect.ManaCost));
}
void UAetherSpellAbility::ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,const FGameplayEventData* Event)
{
    bPaid=false;bResultCommitted=false;PreparedCast.Reset();
    FString Id;int32 Rank=0;
    auto* C=Info?Cast<AAetherCharacter>(Info->AvatarActor.Get()):nullptr;
    if(C)C->BuffRuntime->FlushDue();
    if(C)if(auto* PS=C->GetPlayerState<AAetherPlayerState>())PS->RefreshTemporarySkills();
    if(!IsActive())return;
    FHitResult Hit;FVector Origin,Direction;
    if(!IsValid(C)||!C->HasAuthority()||!ResolveSkill(H,Info,Id,Rank)||!C->FindSkillTarget(Id,Rank,Hit,Origin,Direction))
    {EndAbility(H,Info,A,true,true);return;}
    // 复制身份/等级/成本再 Commit，属性通知可能改变 ASC 列表，不能跨回调持有 Spec 指针。
    FAetherCastExecution Cast;Cast.SkillId=Id;Cast.Rank=Rank;
    const auto& Definitions=FAetherSkillDefinitionsV10::Get();
    Cast.DefinitionRevision=Definitions.ContentSchemaVersion;Cast.Effect=*Definitions.Effect(Id,Rank);
    Cast.Mechanic=Definitions.Skills.FindChecked(Id).Mechanic;
    if(const auto* Receiver=C->ResourceGate->GetReceiver())Cast.LifeId=Receiver->State().LifeId;
    if(auto* Target=::Cast<AAetherCharacter>(Hit.GetActor()))if(const auto* Receiver=Target->ResourceGate->GetReceiver())Cast.TargetLifeId=Receiver->State().LifeId;
    const double Speed=FMath::Clamp(double(C->BuffRuntime->ActionSpeedMultiplier),.1,3.);
    Cast.Effect.WindupSeconds/=Speed;
    Cast.Effect.RecoverySeconds=(Cast.Effect.RecoverySeconds<0?Cast.Effect.Cooldown:Cast.Effect.RecoverySeconds)/Speed;
    PreparedCast=Cast;PaymentExecution=MakeShared<FAetherCastExecution>(Cast);PreparedAvatar=C;PreparedSystem=Info->AbilitySystemComponent;
    C->CastExecutionId=Cast.ExecutionId;
    UE_LOG(LogTemp,Verbose,TEXT("AETHER_SPELL_ACCEPT input=%u execution=%s skill=%s life=%s time=%.3f"),C->LastServerCastInputSequence,*Cast.ExecutionId.ToString(),*Id,*Cast.LifeId.ToString(),C->CombatTime());
    if(Cast.Effect.WindupSeconds>0)
    {
        C->CastStartedAt=C->CombatTime();C->CastLockUntil=C->CastStartedAt+float(Cast.Effect.WindupSeconds);C->ForceNetUpdate();
        GetWorld()->GetTimerManager().SetTimer(WindupTimer,FTimerDelegate::CreateWeakLambda(this,[this,Execution=Cast.ExecutionId]{FinishCast(Execution);}),float(Cast.Effect.WindupSeconds),false);
        return;
    }
    FinishCast(Cast.ExecutionId);
}
void UAetherSpellAbility::FinishCast(FGuid ExpectedExecution)
{
    if(!IsActive()||!PreparedCast.IsSet()||PreparedCast->ExecutionId!=ExpectedExecution)return;
    auto Cast=PreparedCast.GetValue();const FString Id=Cast.SkillId;const int32 Rank=Cast.Rank;
    const auto Payment=PaymentExecution;
    const auto H=CurrentSpecHandle;const auto* Info=CurrentActorInfo;const auto A=CurrentActivationInfo;
    const auto Avatar=PreparedAvatar;const auto ASC=PreparedSystem;
    if(!Avatar.IsValid()||!ASC.IsValid()||ASC->GetAvatarActor()!=Avatar.Get()||Avatar->CastExecutionId!=ExpectedExecution)
    {EndAbility(H,Info,A,true,true);return;}
    Avatar->CastLockUntil=0;Avatar->BuffRuntime->FlushDue();
    const bool Committed=IsActive()&&CommitAbility(H,Info,A);
    Cast.bCostApplied=Payment.IsValid()&&Payment->bCostApplied;
    const auto SameLife=[&]() {
        if(!Avatar.IsValid()||!ASC.IsValid()||ASC->GetAvatarActor()!=Avatar.Get()||Avatar->AbilitySystem!=ASC.Get()||!Avatar->Alive())return false;
        const auto* Receiver=Avatar->ResourceGate->GetReceiver();
        return Cast.LifeId.IsValid() ? Receiver&&Receiver->State().LifeId==Cast.LifeId : !Receiver;
    };
    FString CurrentId;int32 CurrentRank=0;
    const bool OwnsExecution=Payment==PaymentExecution&&PreparedCast.IsSet()&&PreparedCast->ExecutionId==ExpectedExecution;
    const bool Valid=Committed&&OwnsExecution&&IsActive()&&SameLife()&&Avatar->CastExecutionId==ExpectedExecution&&Avatar->Ready()&&ResolveSkill(H,Info,CurrentId,CurrentRank)&&CurrentId==Id&&CurrentRank==Rank;
    Cast.bResultCommitted=Valid&&Avatar->ExecuteCast(Cast);
    if(OwnsExecution)bResultCommitted=Cast.bResultCommitted;
    UE_LOG(LogTemp,Verbose,TEXT("AETHER_SPELL_RESULT execution=%s committed=%d cost=%d"),*Cast.ExecutionId.ToString(),Cast.bResultCommitted,Cast.bCostApplied);
    if(!Cast.bResultCommitted&&Cast.bCostApplied&&Payment.IsValid()&&!Payment->bRefunded&&SameLife())
    {
        Cast.bRefunded=true;Payment->bRefunded=true;
        const auto Refund=[Avatar,ASC,Life=Cast.LifeId,Cost=float(Cast.Effect.ManaCost)] {
            if(!Avatar.IsValid()||!ASC.IsValid()||ASC->GetAvatarActor()!=Avatar.Get()||!Avatar->Alive())return;
            const auto* Receiver=Avatar->ResourceGate->GetReceiver();
            if(Life.IsValid()?(!Receiver||Receiver->State().LifeId!=Life):Receiver!=nullptr)return;
            ASC->ApplyModToAttribute(UAetherAttributes::GetManaAttribute(),EGameplayModOp::Additive,Cost);
        };
        if(!Avatar->ResourceGate->Defer(Refund))Refund();
    }
    if(IsActive()&&Payment==PaymentExecution)EndAbility(H,Info,A,true,!Cast.bResultCommitted);
}
void UAetherSpellAbility::EndAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,bool Replicate,bool Cancelled)
{
    if(!IsEndAbilityValid(H,Info))return;
    if(ScopeLockCount>0)
    {WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this,&UAetherSpellAbility::EndAbility,H,Info,A,Replicate,Cancelled));return;}
    if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(WindupTimer);
    if(PreparedAvatar.IsValid()&&PreparedCast.IsSet()&&PreparedAvatar->CastExecutionId==PreparedCast->ExecutionId)
    {
        if(!bResultCommitted)PreparedAvatar->CastLockUntil=0;
        PreparedAvatar->CastExecutionId.Invalidate();PreparedAvatar->ForceNetUpdate();
    }
    Super::EndAbility(H,Info,A,Replicate,Cancelled);
}
