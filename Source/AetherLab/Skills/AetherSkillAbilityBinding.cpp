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
bool Publish(UAbilitySystemComponent& ASC,const FAetherSkillStateV10& State,const TArray<FAetherExternalSkillGrant>& Grants,FString& Reason)
{
    check(IsInGameThread());const auto& D=FAetherSkillDefinitionsV10::Get();
    if(!ASC.GetOwnerActor()||!ASC.GetOwnerActor()->HasAuthority()||!ASC.GetAvatarActor()||
        !State.Validate(D,Reason)||!FAetherSkillStateV10::ValidateExternalGrants(Grants,D))
    {if(Reason.IsEmpty())Reason=TEXT("Invalid authoritative skill projection");return false;}
    TMap<FString,FGameplayAbilitySpecHandle> Existing;
    // 改动前先检查整组身份，不能更新了一半后才发现重复/未知标签。
    for(const auto& Spec:ASC.GetActivatableAbilities())
    {
        if(!Spec.Ability||Spec.Ability->GetClass()!=UAetherSpellAbility::StaticClass())continue;
        const FString Id=Identify(Spec);
        if(Id.IsEmpty()||Existing.Contains(Id)){Reason=TEXT("Ambiguous skill ability identity");return false;}
        Existing.Add(Id,Spec.Handle);
    }
    TArray<FString> Ids;D.Skills.GenerateKeyArray(Ids);Ids.Sort();
    for(const auto& Id:Ids)
        if(!TagFor(Id).IsValid()){Reason=TEXT("Skill tag unavailable");return false;}
    for(const auto& Id:Ids)
    {
        const auto& Definition=D.Skills.FindChecked(Id);
        const int32 Rank=State.EffectiveRank(Id,Grants);
        const auto* ExistingHandle=Existing.Find(Id);
        if(!Definition.bActive||Rank<=0)
        {
            if(ExistingHandle){ASC.CancelAbilityHandle(*ExistingHandle);ASC.ClearAbility(*ExistingHandle);}
            continue;
        }
        int32 Input=INDEX_NONE;
        for(int32 Slot=0;Slot<FAetherSkillStateV10::HotbarCapacity;++Slot)
            if(const auto* Skill=State.Hotbar.Find(Slot);Skill&&Skill->Equals(Id,ESearchCase::CaseSensitive)){Input=Slot;break;}
        if(ExistingHandle)
        {
            auto* Spec=ASC.FindAbilitySpecFromHandle(*ExistingHandle);
            if(!Spec){Reason=TEXT("Skill spec disappeared during publication");return false;}
            if(Spec->Level!=Rank||Spec->InputID!=Input)
            {
                // 等级/授权变化结束旧执行，不能让旧实例继续用原来已失效的等级计算效果。
                ASC.CancelAbilityHandle(*ExistingHandle);
                Spec=ASC.FindAbilitySpecFromHandle(*ExistingHandle);if(!Spec){Reason=TEXT("Skill removed during cancellation");return false;}
                Spec->Level=Rank;Spec->InputID=Input;ASC.MarkAbilitySpecDirty(*Spec);
            }
        }
        else
        {
            FGameplayAbilitySpec Spec(UAetherSpellAbility::StaticClass(),Rank,Input);
            Spec.GetDynamicSpecSourceTags().AddTag(TagFor(Id));
            if(!ASC.GiveAbility(Spec).IsValid()){Reason=TEXT("Skill grant failed");return false;}
        }
    }
    Reason.Reset();return true;
}

}
