#pragma once
#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AetherSkillCooldownState.generated.h"

USTRUCT()
struct FAetherSkillCooldownDeadline
{
    GENERATED_BODY()
    UPROPERTY() FString Key;
    UPROPERTY() double EndsAt=0;
};

namespace AetherSkillCooldowns
{
    AETHERGAMEPLAY_API double Remaining(const TArray<FAetherSkillCooldownDeadline>& Deadlines,const FString& Skill,const FString& Group,double Now);
    AETHERGAMEPLAY_API bool Commit(TArray<FAetherSkillCooldownDeadline>& Deadlines,const FString& Skill,const FString& Group,double SkillSeconds,double GroupSeconds,double Now);
}

// Definition 技能的唯一冷却状态由其服务器 ASC 拥有，不由 Pawn/AI 决策器镜像。
// NPC 没有拥有者技能面板，故不复制；PlayerState 的既有复制/会话冷却仍独立负责 Profile 权威。
// 此状态通过 GAS CheckCost 查询，成功 ExecuteCast 提交；它不是 GameplayEffect 冷却。
UCLASS()
class AETHERGAMEPLAY_API UAetherDefinitionAbilitySystem : public UAbilitySystemComponent
{
    GENERATED_BODY()
public:
    double CooldownRemaining(const FString& Skill,const FString& Group,double Now) const;
private:
    friend class AAetherCharacter;
    friend class FAetherNpcSkillCooldownTest;
    bool CommitCooldown(const FString& Skill,const FString& Group,double SkillSeconds,double GroupSeconds,double Now);
    UPROPERTY(Transient) TArray<FAetherSkillCooldownDeadline> SkillCooldowns;
};
