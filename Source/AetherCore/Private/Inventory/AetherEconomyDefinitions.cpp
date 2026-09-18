#include "Inventory/AetherEconomyDefinitions.h"
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
bool Names(const TSharedPtr<FJsonObject>& O,const TCHAR* Field,TArray<FString>& Out,int32 Limit)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!O->TryGetArrayField(Field,Values)||Values->Num()>Limit)return false;
    for(const auto& V:*Values){FString S;if(!V->TryGetString(S)||!Id(S)||Out.Contains(S))return false;Out.Add(S);}return true;
}
}
bool FAetherEconomyDefinitionsV10::Validate(const FAetherV10ItemDefinitions& Items,FString& Reason) const
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(!Items.Validate(Reason))return false;
    if(SchemaVersion!=1||Shops.IsEmpty()||Shops.Num()>64)return Fail(TEXT("Invalid economy schema/shops"));
    TSet<FString> Categories;for(const auto& P:Items.Items)Categories.Add(P.Value.Category);
    for(const auto& P:Shops)
    {
        const auto& S=P.Value;
        if(!Id(S.Id)||!S.Id.Equals(P.Key,ESearchCase::CaseSensitive)||S.Products.Num()>128||S.AcceptedCategories.Num()>32||
            S.RepairGoldPerPoint<0||S.RepairGoldPerPoint>1000||S.bRepair!=(S.RepairGoldPerPoint>0))
            return Fail(TEXT("Invalid shop service definition"));
        TSet<FString> Seen;
        for(const auto& Product:S.Products)
        {
            const auto* D=Items.Items.Find(Product);
            if(!D||!D->Id.Equals(Product,ESearchCase::CaseSensitive)||D->BuyPrice<=0||Seen.Contains(Product))return Fail(TEXT("Unknown/duplicate/unpriced shop product"));
            Seen.Add(Product);
        }
        Seen.Reset();
        for(const auto& Category:S.AcceptedCategories)
        {if(!Categories.Contains(Category)||Seen.Contains(Category))return Fail(TEXT("Unknown/duplicate shop category"));Seen.Add(Category);}
        if(S.Products.IsEmpty()&&S.AcceptedCategories.IsEmpty()&&!S.bRepair)return Fail(TEXT("Empty shop service"));
    }
    Reason.Reset();return true;
}
FAetherEconomyDefinitionsV10 FAetherEconomyDefinitionsV10::Parse(const FString& Json,const FAetherV10ItemDefinitions& Items,FString& Reason)
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return FAetherEconomyDefinitionsV10();};
    TSharedPtr<FJsonObject> Root;FAetherEconomyDefinitionsV10 D;double Schema=0;
    if(Json.Len()>256*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Root->TryGetNumberField(TEXT("SchemaVersion"),Schema)||Schema!=1)return Fail(TEXT("Invalid economy JSON/schema"));
    const TArray<TSharedPtr<FJsonValue>>* Shops=nullptr;
    if(!Root->TryGetArrayField(TEXT("Shops"),Shops)||Shops->IsEmpty()||Shops->Num()>64)return Fail(TEXT("Invalid shop array"));
    for(const auto& V:*Shops)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherShopDefinitionV10 S;double Rate=0;
        if(!V->TryGetObject(O)||!O||!O->IsValid()||!(*O)->TryGetStringField(TEXT("Id"),S.Id)||
            !Names(*O,TEXT("Products"),S.Products,128)||!Names(*O,TEXT("AcceptedCategories"),S.AcceptedCategories,32)||
            !(*O)->TryGetBoolField(TEXT("Repair"),S.bRepair)||!(*O)->TryGetNumberField(TEXT("RepairGoldPerPoint"),Rate)||
            !FMath::IsFinite(Rate)||Rate<0||Rate>1000||FMath::FloorToDouble(Rate)!=Rate||D.Shops.Contains(S.Id))
            return Fail(TEXT("Invalid/duplicate shop entry"));
        S.RepairGoldPerPoint=int32(Rate);const auto Key=S.Id;D.Shops.Add(Key,MoveTemp(S));
    }
    if(!D.Validate(Items,Reason))return {};return D;
}
