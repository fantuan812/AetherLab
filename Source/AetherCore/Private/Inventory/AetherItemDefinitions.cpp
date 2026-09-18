#include "Inventory/AetherInventoryState.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool Id(const FString& S)
{
    if(S.IsEmpty()||S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))return false;
    return true;
}
bool Names(const TSharedPtr<FJsonObject>& O,const TCHAR* Field,TArray<FString>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!O->TryGetArrayField(Field,Values)||Values->Num()>32)return false;
    for(const auto& V:*Values){FString S;if(!V->TryGetString(S)||!Id(S)||Out.Contains(S))return false;Out.Add(S);}
    return true;
}
bool Number(const TSharedPtr<FJsonObject>& O,const TCHAR* Field,int32& Out,int32 Max)
{
    double N=0;if(!O->TryGetNumberField(Field,N)||!FMath::IsFinite(N)||N<0||N>Max||FMath::FloorToDouble(N)!=N)return false;
    Out=int32(N);return true;
}
}
const FAetherEquipmentSlotDefinition* FAetherV10ItemDefinitions::FindSlot(const FString& Name) const
{return Slots.FindByPredicate([&](const auto& S){return S.Id.Equals(Name,ESearchCase::CaseSensitive);});}
bool FAetherV10ItemDefinitions::Validate(FString& Reason) const
{
    const auto Reject=[&](const TCHAR* Text){Reason=Text;return false;};
    if(ContentSchemaVersion!=3||DefaultCapacity<1||DefaultCapacity>256||Items.IsEmpty()||Items.Num()>4096||Slots.Num()!=10)
        return Reject(TEXT("Unsupported item schema or definition bounds"));
    const TArray<FString> Required{TEXT("MainHand"),TEXT("OffHand"),TEXT("Head"),TEXT("Chest"),TEXT("Hands"),
        TEXT("Legs"),TEXT("Feet"),TEXT("Neck"),TEXT("Ring1"),TEXT("Ring2")};
    TSet<FString> SlotIds;
    for(const auto& S:Slots)
    {
        if(!Required.ContainsByPredicate([&](const auto& V){return V.Equals(S.Id,ESearchCase::CaseSensitive);})||SlotIds.Contains(S.Id)||S.AllowedCategories.IsEmpty()||S.AllowedCategories.Num()>32)
            return Reject(TEXT("Invalid equipment layout"));
        TSet<FString> Categories;
        for(const auto& C:S.AllowedCategories){if(!Id(C)||Categories.Contains(C))return Reject(TEXT("Invalid slot category"));Categories.Add(C);}
        SlotIds.Add(S.Id);
    }
    const TSet<FString> Stats{TEXT("Damage"),TEXT("Posture"),TEXT("Armor"),TEXT("FireResist"),TEXT("WaterResist"),
        TEXT("FrostResist"),TEXT("StormResist"),TEXT("MaxHealth"),TEXT("MaxMana"),TEXT("MaxStamina")};
    for(const auto& Pair:Items)
    {
        const auto& I=Pair.Value;
        if(!Id(I.Id)||!Pair.Key.Equals(I.Id,ESearchCase::CaseSensitive)||I.DisplayName.IsEmpty()||I.DisplayName.Len()>128||!Id(I.Category)||!Id(I.IconId)||
            I.MaxStack<1||I.MaxStack>1000||I.BuyPrice<0||I.BuyPrice>10000000||I.SellPrice<0||I.SellPrice>10000000||
            (I.BuyPrice>0&&I.SellPrice>=I.BuyPrice)||I.MaxDurability<0||I.MaxDurability>1000000||
            !FMath::IsFinite(I.BrokenStatMultiplier)||I.BrokenStatMultiplier<0||I.BrokenStatMultiplier>1)
            return Reject(TEXT("Invalid item definition"));
        if((!I.AllowedSlots.IsEmpty()&&I.MaxStack!=1)||(I.MaxDurability>0&&I.AllowedSlots.IsEmpty())||
            (I.bSellable&&I.SellPrice<=0)||(!I.EquipmentId.IsEmpty()&&!Id(I.EquipmentId))||(!I.UseId.IsEmpty()&&!Id(I.UseId)))
            return Reject(TEXT("Inconsistent item capabilities"));
        TSet<FString> Allowed;
        for(const auto& S:I.AllowedSlots)
        {
            const auto* Slot=FindSlot(S);
            if(!Slot||!Slot->AllowedCategories.Contains(I.Category)||Allowed.Contains(S))return Reject(TEXT("Invalid allowed equipment slot"));
            Allowed.Add(S);
        }
        TSet<FString> Extra;
        for(const auto& S:I.AdditionalOccupiedSlots)
        {
            if(!FindSlot(S)||Allowed.Contains(S)||Extra.Contains(S)||Allowed.IsEmpty())return Reject(TEXT("Invalid additional slot occupancy"));
            Extra.Add(S);
        }
        for(const auto& Stat:I.Stats)
            if(!Stats.Contains(Stat.Key)||!FMath::IsFinite(Stat.Value)||Stat.Value<0||Stat.Value>10000)
                return Reject(TEXT("Unknown or invalid equipment statistic"));
        if(!I.Stats.IsEmpty()&&I.AllowedSlots.IsEmpty())return Reject(TEXT("Unequippable item has equipment statistics"));
    }
    Reason.Reset();return true;
}
FAetherV10ItemDefinitions FAetherV10ItemDefinitions::Parse(const FString& Json,FString& Reason)
{
    FAetherV10ItemDefinitions D;
    const auto Reject=[&](const TCHAR* Text){Reason=Text;return FAetherV10ItemDefinitions();};
    TSharedPtr<FJsonObject> Root;
    if(Json.Len()>4*1024*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root)
        return Reject(TEXT("Invalid item definition JSON"));
    if(!Number(Root,TEXT("ContentSchemaVersion"),D.ContentSchemaVersion,3)||
        !Number(Root,TEXT("DefaultCapacity"),D.DefaultCapacity,256))return Reject(TEXT("Missing item schema/capacity"));
    const TArray<TSharedPtr<FJsonValue>> *Slots=nullptr,*Items=nullptr;
    if(!Root->TryGetArrayField(TEXT("Slots"),Slots)||Slots->Num()!=10||
        !Root->TryGetArrayField(TEXT("Items"),Items)||Items->IsEmpty()||Items->Num()>4096)
        return Reject(TEXT("Invalid slot/item arrays"));
    for(const auto& V:*Slots)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherEquipmentSlotDefinition S;
        if(!V->TryGetObject(O)||!O||!O->IsValid())return Reject(TEXT("Slot must be an object"));
        if(!O||!(*O)->TryGetStringField(TEXT("Id"),S.Id)||!Names(*O,TEXT("AllowedCategories"),S.AllowedCategories))
            return Reject(TEXT("Invalid equipment slot entry"));
        D.Slots.Add(MoveTemp(S));
    }
    for(const auto& V:*Items)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherV10ItemDefinition I;
        if(!V->TryGetObject(O)||!O||!O->IsValid())return Reject(TEXT("Item must be an object"));
        if(!O||!(*O)->TryGetStringField(TEXT("Id"),I.Id)||!(*O)->TryGetStringField(TEXT("DisplayName"),I.DisplayName)||
            !(*O)->TryGetStringField(TEXT("Category"),I.Category)||!(*O)->TryGetStringField(TEXT("IconId"),I.IconId)||
            !(*O)->TryGetStringField(TEXT("EquipmentId"),I.EquipmentId)||!(*O)->TryGetStringField(TEXT("UseId"),I.UseId)||
            !Number(*O,TEXT("MaxStack"),I.MaxStack,1000)||!Number(*O,TEXT("BuyPrice"),I.BuyPrice,10000000)||
            !Number(*O,TEXT("SellPrice"),I.SellPrice,10000000)||!Number(*O,TEXT("MaxDurability"),I.MaxDurability,1000000)||
            !(*O)->TryGetBoolField(TEXT("Sellable"),I.bSellable)||!(*O)->TryGetBoolField(TEXT("Droppable"),I.bDroppable)||
            !(*O)->TryGetBoolField(TEXT("QuestLocked"),I.bQuestLocked)||!(*O)->TryGetNumberField(TEXT("BrokenStatMultiplier"),I.BrokenStatMultiplier)||
            !Names(*O,TEXT("AllowedSlots"),I.AllowedSlots)||!Names(*O,TEXT("AdditionalOccupiedSlots"),I.AdditionalOccupiedSlots))
            return Reject(TEXT("Incomplete item definition"));
        if(D.Items.Contains(I.Id))return Reject(TEXT("Duplicate item definition ID"));
        const TSharedPtr<FJsonObject>* Stats=nullptr;
        if(!(*O)->TryGetObjectField(TEXT("Stats"),Stats)||(*Stats)->Values.Num()>10)return Reject(TEXT("Invalid equipment stats object"));
        for(const auto& Pair:(*Stats)->Values){double N=0;if(!Pair.Value->TryGetNumber(N))return Reject(TEXT("Invalid stat value"));I.Stats.Add(FString(*Pair.Key),N);}
        const FString ItemId=I.Id;D.Items.Add(ItemId,MoveTemp(I));
    }
    if(!D.Validate(Reason))return FAetherV10ItemDefinitions();
    return D;
}
