#pragma once
#include "CoreMinimal.h"
#include "GameplayAbilitySpec.h"
#include "Skills/AetherSkillState.h"
class UAbilitySystemComponent;

// SkillId 通过 Spec 的原生复制标签承载。等级和输入位都不是身份。
// 当前桥接位于旧游戏模块，领域技能/点数规则仍留在不依赖 GAS 的 AetherCore。
namespace AetherSkillBinding
{
    void RegisterDefinitions();
    AETHERGAMEPLAY_API FGameplayTag TagFor(const FString& SkillId);
    AETHERGAMEPLAY_API FString Identify(const FGameplayAbilitySpec& Spec);
    AETHERGAMEPLAY_API FGameplayAbilitySpec* Find(UAbilitySystemComponent& ASC,const FString& SkillId);
    // 提交后的永久账本 + 服务器重建的外部来源 -> 唯一 Spec；只改本模块拥有的技能，不触碰闪避/近战。
    AETHERGAMEPLAY_API bool Publish(UAbilitySystemComponent& ASC,const FAetherSkillStateV10& State,
        const TArray<FAetherExternalSkillGrant>& Grants,FString& Reason);
}
