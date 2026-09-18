#include "AetherInventoryRules.h"
#include "AetherProfile.h"
#include "AetherRules.h"
namespace AetherInventory
{
bool ValidateLoadout(const FAetherInventoryData& P,const FAetherRules& R)
{
 if(P.Equipped.Num()>8)return false;TSet<FName> Occupied;TSet<FGuid> Instances;
 for(const auto& Pair:P.Equipped)
 {
  const auto* Item=P.Inventory.FindByPredicate([&](const auto& I){return I.InstanceId==Pair.Value;});
  const auto* Rule=Item?R.Items.Find(Item->DefinitionId):nullptr;
  if(!Rule||!Rule->bPlayerEquippable||Rule->EquipmentId.IsNone()||Rule->Slot!=Pair.Key||Item->Count!=1||Instances.Contains(Pair.Value))return false;
  Instances.Add(Pair.Value);
  for(FName Slot:Rule->OccupiedSlots){if(Occupied.Contains(Slot))return false;Occupied.Add(Slot);}
 }
 return true;
}
bool Equip(FAetherInventoryData& P,FGuid Id,const FAetherRules& R)
{
 const auto* Item=P.Inventory.FindByPredicate([&](const auto& I){return I.InstanceId==Id;});
 const auto* Rule=Item?R.Items.Find(Item->DefinitionId):nullptr;
 if(!Rule||!Rule->bPlayerEquippable||Item->Count!=1)return false;
 auto Next=P;TArray<FName> Remove;
 for(const auto& Pair:P.Equipped)
 {
  if(Pair.Key==Rule->Slot){Remove.Add(Pair.Key);continue;}
  const auto* Existing=P.Inventory.FindByPredicate([&](const auto& I){return I.InstanceId==Pair.Value;});
  const auto* Other=Existing?R.Items.Find(Existing->DefinitionId):nullptr;if(!Other)return false;
  // An occupied primary slot rejects insertion; equipping a larger item clears secondary slots.
  if(Other->OccupiedSlots.Contains(Rule->Slot))return false;
  for(FName Slot:Rule->OccupiedSlots)if(Other->OccupiedSlots.Contains(Slot)){Remove.AddUnique(Pair.Key);break;}
 }
 for(FName Slot:Remove)Next.Equipped.Remove(Slot);Next.Equipped.Add(Rule->Slot,Id);
 if(!ValidateLoadout(Next,R))return false;P.Equipped=MoveTemp(Next.Equipped);return true;
}
bool BuildLoadout(const FAetherInventoryData& P,const FAetherRules& R,TArray<FAetherEquippedSlot>& Out)
{
 if(!ValidateLoadout(P,R))return false;TArray<FAetherEquippedSlot> Candidate;
 for(const auto& Pair:P.Equipped)for(const auto& Item:P.Inventory)if(Item.InstanceId==Pair.Value)
 {FAetherEquippedSlot S;S.Slot=Pair.Key;S.ItemId=R.Items.FindChecked(Item.DefinitionId).EquipmentId;Candidate.Add(S);}
 Out=MoveTemp(Candidate);return true;
}
bool OwnsEquipment(const FAetherInventoryData& P,FName Id,const FAetherRules& R)
{
 for(const auto& Item:P.Inventory)if(const auto* Rule=R.Items.Find(Item.DefinitionId);Rule&&Rule->bPlayerEquippable&&Rule->EquipmentId==Id&&Item.Count>0)return true;
 return false;
}
bool ValidateCatalog(const FAetherRules& R,const UAetherEquipmentCatalog* Catalog)
{
 if(!Catalog||!Catalog->IsValidCatalog())return false;
 for(const auto& Pair:R.Items)if(Pair.Value.bPlayerEquippable)
 {
  const auto& Rule=Pair.Value;const auto* D=Catalog->Find(Rule.EquipmentId);
  if(!D||D->Slot!=Rule.Slot||Rule.OccupiedSlots.Num()!=(D->bOccupiesBothHands?2:1)
      ||!Rule.OccupiedSlots.Contains(D->Slot)||(D->bOccupiesBothHands&&!Rule.OccupiedSlots.Contains("OffHand")))return false;
 }
 return true;
}
}
