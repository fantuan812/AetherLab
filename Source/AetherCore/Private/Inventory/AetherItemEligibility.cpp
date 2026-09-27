#include "Inventory/AetherItemEligibility.h"

EAetherUseAvailability AetherItemEligibility::QueryUse(const FAetherUseRule& Rule,const FAetherUseSummary& S)
{
    using E=EAetherUseAvailability;if(!S.bKnown)return E::Unknown;
    if(!S.bCanAct||S.Health<=0)return E::Restricted;
    if(S.CooldownRemaining>0)return E::Cooldown;
    if(S.SafeForSeconds<Rule.SafeSeconds)return E::Unsafe;
    if((Rule.Health<=0||S.Health>=S.MaxHealth)&&(Rule.Mana<=0||S.Mana>=S.MaxMana)&&(Rule.Stamina<=0||S.Stamina>=S.MaxStamina))return E::NoBenefit;
    return E::Allowed;
}
FString AetherItemEligibility::UseReason(EAetherUseAvailability R)
{
    switch(R)
    {
    case EAetherUseAvailability::Allowed:return {};
    case EAetherUseAvailability::Unknown:return TEXT("等待服务器资源同步。");
    case EAetherUseAvailability::Cooldown:return TEXT("药剂共享冷却尚未结束。");
    case EAetherUseAvailability::Unsafe:return TEXT("近期受到伤害，请等待安全时间。");
    case EAetherUseAvailability::NoBenefit:return TEXT("当前资源已满，使用没有收益。");
    default:return TEXT("当前动作或倒地状态不可使用。");
    }
}

EAetherInventoryMutationCode AetherItemEligibility::Query(const FAetherInventoryStateV10& Inventory,
    FGuid Id,const FString& Owner,EAetherItemOperation Operation,const FAetherV10ItemDefinitions& Definitions)
{
    using E=EAetherInventoryMutationCode;
    const auto* Item=Inventory.Find(Id);if(!Item)return E::Missing;
    const auto* Def=Definitions.Items.Find(Item->DefinitionId);if(!Def||Owner.IsEmpty())return E::Invalid;
    if(Inventory.IsEquipped(Id))return E::Equipped;
    if(!Item->BoundToCharacter.IsEmpty()&&!Item->BoundToCharacter.Equals(Owner,ESearchCase::CaseSensitive))return E::Bound;
    if(Operation==EAetherItemOperation::PersonalStorage||Operation==EAetherItemOperation::Withdraw)return E::Applied;
    if(Operation==EAetherItemOperation::Sell)return Inventory.CanRemove(Id,Owner,true,Definitions);
    if(Operation==EAetherItemOperation::Drop||Operation==EAetherItemOperation::SharedStorage)
        return Inventory.CanRemove(Id,Owner,false,Definitions);
    if(Item->bLocked||Def->bQuestLocked||Item->QuestInstanceId.IsValid())return E::Locked;
    return Def->UseId.IsEmpty()?E::NotAllowed:E::Applied;
}
