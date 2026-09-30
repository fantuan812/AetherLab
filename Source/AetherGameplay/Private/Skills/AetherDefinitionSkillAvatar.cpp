#include "Combat/AetherCombat.h"
#include "Skills/AetherNpcSkillDefinitions.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Framework/AetherProgression.h"
#include "AbilitySystemComponent.h"

bool AAetherCharacter::GrantDefinitionSkills(FString& Reason)
{
    if(!HasAuthority()||SkillAuthority!=EAetherSkillAuthority::Definition||GetPlayerState<AAetherPlayerState>()||
        !AbilitySystem||AbilitySystem->GetAvatarActor()!=this)
    {Reason=TEXT("Definition grants require a server-owned NPC ability system, never a persistent player profile");return false;}
    const auto* Loadout=FAetherNpcSkillDefinitions::Get().Find(SkillLoadoutId);
    if(!Loadout){Reason=TEXT("Missing NPC skill capability definition; no default spell grants");return false;}
    TSet<FString> ExistingIdentities;
    for(const auto& Spec:AbilitySystem->GetActivatableAbilities())
    {
        if(!Spec.Ability||Spec.Ability->GetClass()!=UAetherSpellAbility::StaticClass())continue;
        const FString Id=AetherSkillBinding::Identify(Spec);
        if(Id.IsEmpty()||ExistingIdentities.Contains(Id)){Reason=TEXT("Ambiguous existing NPC skill identity");return false;}
        ExistingIdentities.Add(Id);
    }
    for(const auto& Grant:Loadout->InitialGrants)if(!AetherSkillBinding::TagFor(Grant.SkillId).IsValid())
    {Reason=TEXT("NPC capability identity tag unavailable");return false;}
    TArray<FGameplayAbilitySpecHandle> Remove;
    for(const auto& Spec:AbilitySystem->GetActivatableAbilities())
    {
        const FString Id=AetherSkillBinding::Identify(Spec);
        if(!Id.IsEmpty()&&!Loadout->InitialGrants.ContainsByPredicate([&](const auto& G){return G.SkillId.Equals(Id,ESearchCase::CaseSensitive);}))Remove.Add(Spec.Handle);
    }
    for(const auto Handle:Remove){AbilitySystem->CancelAbilityHandle(Handle);AbilitySystem->ClearAbility(Handle);}
    for(const auto& Grant:Loadout->InitialGrants)
    {
        // Initial grants are idempotent; rank changes subsequently owned by server GAS are retained.
        if(auto* Existing=AetherSkillBinding::Find(*AbilitySystem,Grant.SkillId))continue;
        const auto Tag=AetherSkillBinding::TagFor(Grant.SkillId);
        if(!Tag.IsValid()){Reason=TEXT("NPC capability identity tag unavailable");return false;}
        FGameplayAbilitySpec Spec(UAetherSpellAbility::StaticClass(),Grant.Rank,Grant.Slot);
        Spec.GetDynamicSpecSourceTags().AddTag(Tag);
        if(!AbilitySystem->GiveAbility(Spec).IsValid()){Reason=TEXT("NPC capability grant failed");return false;}
    }
    Reason.Reset();return true;
}
FString AAetherCharacter::SkillAtInputSlot(int32 Slot) const
{
    if(Slot<0||Slot>=FAetherSkillStateV10::HotbarCapacity||!AbilitySystem)return {};
    FString Result;
    for(const auto& Spec:AbilitySystem->GetActivatableAbilities())if(Spec.InputID==Slot)
    {
        const FString Id=AetherSkillBinding::Identify(Spec);if(Id.IsEmpty())continue;
        if(!Result.IsEmpty())return {};Result=Id;
    }
    return Result;
}
