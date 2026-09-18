#pragma once
#include "CoreMinimal.h"
#include "GameplayAbilitySpec.h"
class UAbilitySystemComponent;

// SkillId 通过 Spec 的原生复制标签承载。等级和输入位都不是身份。
// 当前桥接位于旧游戏模块，领域技能/点数规则仍留在不依赖 GAS 的 AetherCore。
namespace AetherSkillBinding
{
    void RegisterDefinitions();
    AETHERLAB_API FGameplayTag TagFor(const FString& SkillId);
    AETHERLAB_API FString Identify(const FGameplayAbilitySpec& Spec);
    AETHERLAB_API FGameplayAbilitySpec* Find(UAbilitySystemComponent& ASC,const FString& SkillId);
}
