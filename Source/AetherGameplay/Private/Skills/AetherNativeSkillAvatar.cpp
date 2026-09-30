#include "Characters/AetherFrontierCharacter.h"
#include "Networking/AetherCommandClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Skills/AetherSkillDefinitions.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Effects/AetherBuffRuntime.h"

const FAetherSkillStateV10* AAetherFrontierCharacter::NativeSkillView() const
{
    if(HasAuthority()){const auto* PS=ProfileState();return PS?PS->GetNativeSkills():nullptr;}
    const auto* PC=Cast<APlayerController>(GetController());auto* LP=PC?PC->GetLocalPlayer():nullptr;
    if(!LP||PC->GetPawn()!=this)return nullptr;
    const auto& P=LP->GetSubsystem<UAetherCommandClient>()->GetProfile();return P.IsSet()?&P->Skills:nullptr;
}
bool AAetherFrontierCharacter::SkillUnlocked(const FString& Id) const
{
    if(SkillAuthority==EAetherSkillAuthority::Definition)return Super::SkillUnlocked(Id);
    if(SkillAuthority!=EAetherSkillAuthority::Profile)return false;
    const auto* State=NativeSkillView();const auto* Definition=FAetherSkillDefinitionsV10::Get().Skills.Find(Id);
    if(!State||!Definition||!Definition->SkillId.Equals(Id,ESearchCase::CaseSensitive)||!Definition->bActive)return false;
    if(HasAuthority())
    {
        const auto* PS=ProfileState();
        return PS&&State->EffectiveRank(Id,PS->GetNativeSkillGrants())>0;
    }
    const auto* PS=ProfileState();if(!PS||!BuffRuntime->PresentationReady(PS->SkillGrants.ProfileRevision))return false;
    const int32 Rank=State->EffectiveRank(Id,PS->GetNativeSkillGrants());
    const auto* Spec=AbilitySystem?AetherSkillBinding::Find(*AbilitySystem,Id):nullptr;
    // 快照 RPC 与 GAS 复制没有跨通道先后保证；等级尚未追上时不按旧等级预测新技能。
    return Rank>0&&Spec&&Spec->Level==Rank;
}
bool AAetherFrontierCharacter::TrySpell(int32 Slot)
{
    if(SkillAuthority==EAetherSkillAuthority::Definition)return Super::TrySpell(Slot);
    if(SkillAuthority!=EAetherSkillAuthority::Profile)return false;
    const auto* State=NativeSkillView();const auto* Id=State?State->Hotbar.Find(Slot):nullptr;
    return Slot>=0&&Slot<FAetherSkillStateV10::HotbarCapacity&&Id&&TrySkill(*Id);
}
void AAetherFrontierCharacter::GrantSpells()
{
    // 玩家未载入账本时只授予公共动作，不先获得NPC元素技能再撤销。
    if(SkillAuthority==EAetherSkillAuthority::Definition){Super::GrantSpells();return;}
    GrantCoreAbilities();
    if(HasAuthority()&&SkillAuthority==EAetherSkillAuthority::Profile)
        if(auto* PS=ProfileState();PS&&PS->GetNativeProfile()){FString Reason;PS->RebindNativeSkills(Reason);}
}
