#pragma once
#include "CoreMinimal.h"

class AAetherCharacter;
class AController;
class UAbilitySystemComponent;
struct FAetherCastExecution;

enum class EAetherNpcSkillChoice:uint8 { Unavailable, NoOffensiveSkills, Waiting, Approach, Ready };
struct FAetherNpcSkillChoice
{
    EAetherNpcSkillChoice Kind=EAetherNpcSkillChoice::Unavailable;
    FString SkillId;
    double RangeCm=0;
};

// 单一 Think 入口使用的选择状态，不 Tick、不授予技能、不保存成本/伤害/冷却。
class AETHERGAMEPLAY_API FAetherNpcSkillDecision
{
public:
    static bool IsControlled(const AAetherCharacter& Character);
    static bool CanTarget(const AAetherCharacter& Character,const AAetherCharacter& Target);
    FAetherNpcSkillChoice Choose(AAetherCharacter& Character,AAetherCharacter& Target);
    bool TryExecute(AAetherCharacter& Character,AAetherCharacter& Target,const FString& SkillId);
    bool ValidateCommit(AAetherCharacter& Character,const FAetherCastExecution& Cast);
    void RecordCommitted(const AAetherCharacter& Character,const FAetherCastExecution& Cast);
    void CancelRequest(FGuid ActiveExecution=FGuid());
    void Reset();
    const FString& LastCommittedSkill() const{return LastCommittedSkillId;}
private:
    bool SynchronizeControl(const AAetherCharacter& Character);
    bool SameControl(const AAetherCharacter& Character) const;
    void ClearRequest();
    TWeakObjectPtr<UAbilitySystemComponent> OwnedSystem;
    TWeakObjectPtr<AController> OwnedController;
    TWeakObjectPtr<AAetherCharacter> RequestedTarget;
    FString LastCommittedSkillId,RequestedSkillId;
    FGuid RequestedExecution;
    // 仅拒绝本 helper 已取消的真实执行；控制权 Reset 不能重新放行旧回调。
    FGuid CanceledExecution;
    bool bIssuingRequest=false;
};
