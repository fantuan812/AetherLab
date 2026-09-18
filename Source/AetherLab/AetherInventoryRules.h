#pragma once
#include "CoreMinimal.h"
#include "AetherEquipmentComponent.h"
struct FAetherInventoryData;struct FAetherRules;
namespace AetherInventory
{
 bool Equip(FAetherInventoryData& Profile,FGuid Instance,const FAetherRules& Rules);
 bool ValidateLoadout(const FAetherInventoryData& Profile,const FAetherRules& Rules);
 bool BuildLoadout(const FAetherInventoryData& Profile,const FAetherRules& Rules,TArray<FAetherEquippedSlot>& Out);
 bool OwnsEquipment(const FAetherInventoryData& Profile,FName EquipmentId,const FAetherRules& Rules);
 bool ValidateCatalog(const FAetherRules& Rules,const UAetherEquipmentCatalog* Catalog);
}
