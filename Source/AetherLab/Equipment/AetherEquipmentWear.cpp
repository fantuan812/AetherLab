#include "AetherCombat.h"
#include "AetherProgression.h"
#include "Networking/AetherCommandRuntime.h"
#include "Definitions/AetherV10Definitions.h"
#include "Inventory/AetherResourceGate.h"
#include "Engine/GameInstance.h"

void AAetherCharacter::RecordEquipmentWear(bool Weapon,bool Guard)
{
    if(!HasAuthority()||!Equipment||!Equipment->bProfileManaged)return;
    const auto* PS=GetPlayerState<AAetherPlayerState>();
    const auto* Profile=PS?PS->GetNativeProfile():nullptr;
    if(!Profile)return;
    FAetherServerFact Event;Event.Kind=EAetherServerFactKind::EquipmentWear;
    Event.CharacterId=Profile->CharacterId;Event.FactId=TEXT("EquipmentWear");
    const auto& Definitions=FAetherV10Definitions::Get().Items;
    for(const auto& Slot:Equipment->Slots)
    {
        const bool Hand=Slot.Slot==TEXT("MainHand")||Slot.Slot==TEXT("OffHand");
        // 一次真实接触磨损主手；格挡优先副手，没有副手则磨损主手；受伤磨损防具。
        const bool HasOffhand=Profile->Inventory.Equipment.Contains(TEXT("OffHand"));
        if(Weapon?Slot.Slot!=TEXT("MainHand"):Guard?Slot.Slot!=(HasOffhand?FName(TEXT("OffHand")):FName(TEXT("MainHand"))):Hand)continue;
        const auto* Item=Profile->Inventory.Find(Slot.InstanceId);
        const auto* Def=Item?Definitions.Items.Find(Item->DefinitionId):nullptr;
        if(Def&&Def->MaxDurability>0)Event.WornItems.AddUnique(Slot.InstanceId);
    }
    if(Event.WornItems.IsEmpty())return;
    auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>();FString Why;
    // 背压不能默默免除耐久。故障关闭资源门并断开连接，阻止继续产生未能记录的战斗事实。
    if(!Runtime||!Runtime->ObserveServerFact(MoveTemp(Event),Why))
        ResourceGate->Fault(TEXT("Equipment wear could not be queued: ")+Why);
}
