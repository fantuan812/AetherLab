#include "Definitions/AetherContainerDefinitions.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
FAetherContainerDefinitions FAetherContainerDefinitions::Parse(const FString& Json)
{
    FAetherContainerDefinitions D;D.Error=TEXT("Invalid static container definitions");
    if(Json.Len()>65536)return D;
    TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;double Version=0;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
       !Root->TryGetNumberField(TEXT("schema"),Version)||Version!=1||
       !Root->TryGetArrayField(TEXT("containers"),Rows)||Rows->IsEmpty()||Rows->Num()>32)return D;
    TSet<FString> Ids;
    auto Stable=[](const FString& Id)
    {if(Id.IsEmpty()||Id.Len()>64)return false;for(TCHAR C:Id)if(!FChar::IsAlnum(C)&&C!='_')return false;return true;};
    for(const auto& Value:*Rows)
    {
        const TSharedPtr<FJsonObject>* Object=nullptr;
        if(!Value->TryGetObject(Object)||!Object||!Object->IsValid())return D;
        const auto& O=*Object;FAetherStaticContainerDefinition R;FString Kind;
        const TArray<TSharedPtr<FJsonValue>>* Position=nullptr;double Capacity=0;
        if(!O->TryGetStringField(TEXT("id"),R.Id)||!Stable(R.Id)||Ids.Contains(R.Id.ToLower())||
           !O->TryGetStringField(TEXT("label"),R.Label)||R.Label.IsEmpty()||R.Label.Len()>96||
           !O->TryGetStringField(TEXT("region"),R.Region)||!Stable(R.Region)||
           !O->TryGetStringField(TEXT("kind"),Kind)||(Kind!=TEXT("shared")&&Kind!=TEXT("personal"))||
           !O->TryGetNumberField(TEXT("capacity"),Capacity)||Capacity<1||Capacity>256||FMath::FloorToDouble(Capacity)!=Capacity||
           !O->TryGetArrayField(TEXT("position"),Position)||Position->Num()!=3)return D;
        double Coordinates[3];for(int32 I=0;I<3;++I)
            if(!(*Position)[I]->TryGetNumber(Coordinates[I])||!FMath::IsFinite(Coordinates[I])||FMath::Abs(Coordinates[I])>100000)return D;
        R.Kind=Kind==TEXT("personal")?EAetherContainerKind::PersonalStorage:EAetherContainerKind::SharedChest;
        if(R.Kind==EAetherContainerKind::PersonalStorage&&!R.Id.EndsWith(TEXT("_")))return D;
        R.Capacity=int32(Capacity);R.Location=FVector(Coordinates[0],Coordinates[1],Coordinates[2]);
        Ids.Add(R.Id.ToLower());D.Containers.Add(MoveTemp(R));
    }
    // 个人实例由 prefix + 已验证角色键形成，前缀必须互不包含，且不能覆盖共享 ID。
    for(const auto& A:D.Containers)for(const auto& B:D.Containers)if(&A!=&B&&A.Kind==EAetherContainerKind::PersonalStorage&&B.Id.StartsWith(A.Id,ESearchCase::IgnoreCase))return D;
    D.bValid=true;D.Error.Reset();return D;
}
