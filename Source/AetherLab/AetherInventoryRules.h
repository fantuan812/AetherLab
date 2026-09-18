#pragma once
#include "CoreMinimal.h"
#include "AetherEquipmentComponent.h"
struct FAetherProfile;struct FAetherRules;
namespace AetherInventory
{
 bool Equip(FAetherProfile& Profile,FGuid Instance,const FAetherRules& Rules);
 bool ValidateLoadout(const FAetherProfile& Profile,const FAetherRules& Rules);
 bool BuildLoadout(const FAetherProfile& Profile,const FAetherRules& Rules,TArray<FAetherEquippedSlot>& Out);
 bool OwnsEquipment(const FAetherProfile& Profile,FName EquipmentId,const FAetherRules& Rules);
 bool ValidateCatalog(const FAetherRules& Rules,const UAetherEquipmentCatalog* Catalog);
}
