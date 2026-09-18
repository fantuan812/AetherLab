#include "Skills/AetherSkillAbilityBinding.h"
#include "Skills/AetherSkillDefinitions.h"
#include "AetherCombat.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagsManager.h"

namespace AetherSkillBinding
{
namespace { TMap<FString,FGameplayTag> SkillTags; }
void RegisterDefinitions()
{
    const auto& Definitions=FAetherSkillDefinitionsV10::Get();FString Reason;
    // 无效规范数据不能静默退回旧常量，否则客户端详情与服务器效果会分叉。
    if(!Definitions.Validate(Reason))
    {
        UE_LOG(LogTemp,Fatal,TEXT("Invalid v10 skill definitions: %s"),*Reason);
        return;
    }
    TArray<FString> Keys;Definitions.Skills.GetKeys(Keys);Keys.Sort();
    for(const auto& Id:Keys)
    {
        const auto Tag=UGameplayTagsManager::Get().AddNativeGameplayTag(
            FName(*(FString(TEXT("Aether.Skill."))+Id)),TEXT("Stable skill identity; Spec Level is rank"));
        checkf(Tag.IsValid(),TEXT("Skill tag registration must precede native tag finalization"));
        SkillTags.Add(Id,Tag);
    }
}
FGameplayTag TagFor(const FString& SkillId) { return SkillTags.FindRef(SkillId); }
FString Identify(const FGameplayAbilitySpec& Spec)
{
    if(!Spec.Ability||Spec.Ability->GetClass()!=UAetherSpellAbility::StaticClass())return {};
    FString Found;
    for(const auto& Tag:Spec.GetDynamicSpecSourceTags())
    {
        if(!Tag.ToString().StartsWith(TEXT("Aether.Skill.")))continue;
        const FString* Id=SkillTags.FindKey(Tag);
        // 未知或多重身份一律拒绝，不能依赖容器顺序猜测使用哪一个技能。
        if(!Id||!Found.IsEmpty())return {};
        Found=*Id;
    }
    return Found;
}
FGameplayAbilitySpec* Find(UAbilitySystemComponent& ASC,const FString& SkillId)
{
    if(SkillId.IsEmpty())return nullptr;
    FGameplayAbilitySpec* Found=nullptr;
    for(auto& Spec:ASC.GetActivatableAbilities())
        if(Identify(Spec)==SkillId)
        {
            if(Found)return nullptr; // 重复授权同样不应随机选中某个等级。
            Found=&Spec;
        }
    return Found;
}
}
