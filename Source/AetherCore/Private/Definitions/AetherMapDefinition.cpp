#include "Definitions/AetherMapDefinition.h"
#include "Definitions/AetherRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
FAetherMapDefinitions FAetherMapDefinitions::Parse(const FString& Json)
{
    FAetherMapDefinitions R;R.Error=TEXT("地图数据无效");
    TSharedPtr<FJsonObject> O;const TArray<TSharedPtr<FJsonValue>> *Regions=nullptr,*Beacons=nullptr;double Version=0;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),O)||!O||!O->TryGetNumberField(TEXT("schema"),Version)||Version!=1||
       !O->TryGetArrayField(TEXT("regions"),Regions)||Regions->Num()>64||!O->TryGetArrayField(TEXT("beacons"),Beacons)||Beacons->Num()>64)return R;
    auto Numbers=[](const TSharedPtr<FJsonObject>& J,const TCHAR* Key,int32 Count,TArray<double>& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!J||!J->TryGetArrayField(Key,A)||A->Num()!=Count)return false;
        Out.Reset();for(const auto& V:*A){double N=0;if(!V->TryGetNumber(N)||!FMath::IsFinite(N)||FMath::Abs(N)>100000)return false;Out.Add(N);}return true;
    };
    for(const auto& V:*Regions)
    {
        const auto J=V->AsObject();FAetherMapRegion Row;TArray<double> C,E,Color;
        if(!J||!J->TryGetStringField(TEXT("label"),Row.Label)||Row.Label.IsEmpty()||Row.Label.Len()>64||!Numbers(J,TEXT("center"),2,C)||
           !Numbers(J,TEXT("extent"),2,E)||E[0]<=0||E[1]<=0||!Numbers(J,TEXT("color"),3,Color))return R;
        for(double N:Color)if(N<0||N>1)return R;
        Row.Center=FVector2D(C[0],C[1]);Row.Extent=FVector2D(E[0],E[1]);Row.Color=FLinearColor(Color[0],Color[1],Color[2]);R.Regions.Add(Row);
    }
    TSet<FName> Ids;
    for(const auto& V:*Beacons)
    {
        const auto J=V->AsObject();FAetherMapBeacon B;FString Id,Quest;TArray<double> P;
        if(!J||!J->TryGetStringField(TEXT("id"),Id)||Id.IsEmpty()||Id.Len()>64||Ids.Contains(FName(Id))||
           !J->TryGetStringField(TEXT("label"),B.Label)||B.Label.IsEmpty()||B.Label.Len()>64||!J->TryGetStringField(TEXT("requiredQuest"),Quest)||!Numbers(J,TEXT("position"),3,P))return R;
        for(TCHAR C:Id)if(!FChar::IsAlnum(C)&&C!='_')return R;
        if(!Quest.IsEmpty()&&!FAetherRules::Get().Quest(FName(Quest)))return R;
        B.Id=FName(Id);B.RequiredQuest=FName(Quest);B.Position=FVector(P[0],P[1],P[2]);Ids.Add(B.Id);R.Beacons.Add(B);
    }
    R.bValid=!R.Regions.IsEmpty()&&!R.Beacons.IsEmpty();if(R.bValid)R.Error.Reset();return R;
}
const FAetherMapDefinitions& FAetherMapDefinitions::Get()
{
    static auto R=[](){FString T;FFileHelper::LoadFileToString(T,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Map.json")));return Parse(T);}();return R;
}
