#include "Characters/AetherFrontierCharacter.h"
#include "Networking/AetherCommandClient.h"
#include "Persistence/AetherNativePersistence.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Skills/AetherSkillDefinitions.h"
#include "Skills/AetherSkillAbilityBinding.h"

bool AAetherFrontierCharacter::UsesNativeSkills() const
{
    const auto* PS=ProfileState();if(!PS)return false;
    if(PS->bNativeSkillsEnabled)return true;
    if(HasAuthority())if(auto* GI=GetGameInstance())return GI->GetSubsystem<UAetherNativePersistence>()->OwnsWriteAuthority();
    if(const auto* PC=Cast<APlayerController>(GetController()))
        if(auto* LP=PC->GetLocalPlayer())return LP->GetSubsystem<UAetherCommandClient>()->GetChannel().IsValid();
    return false;
}
const FAetherSkillStateV10* AAetherFrontierCharacter::NativeSkillView() const
{
    if(HasAuthority()){const auto* PS=ProfileState();return PS?PS->GetNativeSkills():nullptr;}
    const auto* PC=Cast<APlayerController>(GetController());auto* LP=PC?PC->GetLocalPlayer():nullptr;
    if(!LP||PC->GetPawn()!=this)return nullptr;
    const auto& P=LP->GetSubsystem<UAetherCommandClient>()->GetProfile();return P.IsSet()?&P->Skills:nullptr;
}
bool AAetherFrontierCharacter::SkillUnlocked(const FString& Id) const
{
    if(!UsesNativeSkills())return Super::SkillUnlocked(Id);
    const auto* State=NativeSkillView();const auto* Definition=FAetherSkillDefinitionsV10::Get().Skills.Find(Id);
    if(!State||!Definition||!Definition->SkillId.Equals(Id,ESearchCase::CaseSensitive)||!Definition->bActive)return false;
    if(HasAuthority())
    {
        const auto* PS=ProfileState();
        return PS&&State->EffectiveRank(Id,PS->GetNativeSkillGrants())>0;
    }
    // 拥有者的临时来源 DTO 尚未接入之前，不凭界面或 Spec 索引猜测临时授权。
    const int32 Rank=State->PermanentRank(Id);
    const auto* Spec=AbilitySystem?AetherSkillBinding::Find(*AbilitySystem,Id):nullptr;
    // 快照 RPC 与 GAS 复制没有跨通道先后保证；等级尚未追上时不按旧等级预测新技能。
    return Rank>0&&Spec&&Spec->Level==Rank;
}
bool AAetherFrontierCharacter::TrySpell(int32 Slot)
{
    if(!UsesNativeSkills())return Super::TrySpell(Slot);
    const auto* State=NativeSkillView();const auto* Id=State?State->Hotbar.Find(Slot):nullptr;
    return Slot>=0&&Slot<FAetherSkillStateV10::HotbarCapacity&&Id&&TrySkill(*Id);
}
void AAetherFrontierCharacter::GrantSpells()
{
    // 保留公共闪避/近战等基础能力，再按原生账本清理/重建元素技能。
    // 载入期间即使底层存在基础 Spec，SkillUnlocked 仍拒绝原生尚未就绪的角色。
    Super::GrantSpells();
    if(HasAuthority())if(auto* PS=ProfileState();PS&&PS->bNativeSkillsEnabled){FString Reason;PS->RebindNativeSkills(Reason);}
}
