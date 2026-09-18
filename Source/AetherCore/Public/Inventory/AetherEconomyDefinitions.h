#pragma once
#include "Inventory/AetherInventoryState.h"
struct FAetherShopDefinitionV10
{
    FString Id;
    TArray<FString> Products,AcceptedCategories;
    bool bRepair=false;
    int32 RepairGoldPerPoint=0;
};
struct AETHERCORE_API FAetherEconomyDefinitionsV10
{
    int32 SchemaVersion=1;
    TMap<FString,FAetherShopDefinitionV10> Shops;
    bool Validate(const FAetherV10ItemDefinitions& Items,FString& Reason) const;
    static FAetherEconomyDefinitionsV10 Parse(const FString& Json,const FAetherV10ItemDefinitions& Items,FString& Reason);
};
