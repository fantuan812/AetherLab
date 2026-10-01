#include "AI/AetherNpcPerceptionDefinitions.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <initializer_list>
namespace
{
bool Fields(const FJsonObject& O,std::initializer_list<const TCHAR*> Keys)
{
    if(O.Values.Num()!=int32(Keys.size()))return false;
    for(const auto& P:O.Values){bool Found=false;for(const auto* K:Keys)Found|=P.Key.Equals(K,ESearchCase::CaseSensitive);if(!Found)return false;}return true;
}
bool ValidId(const FString& Id)
{
    if(Id.IsEmpty()||Id.Len()>96)return false;
    for(TCHAR C:Id)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='.'||C=='_'))return false;
    return true;
}
bool Number(const FJsonObject& Object,const TCHAR* Key,double& Out)
{return Object.HasTypedField<EJson::Number>(Key)&&Object.TryGetNumberField(Key,Out);}
bool Positive(double Value){return FMath::IsFinite(Value)&&Value>0&&FMath::IsFinite(Value*Value);}
}
bool FAetherNpcPerceptionProfile::IsValid() const
{
    return ValidId(Id)&&Positive(SampleIntervalSeconds)&&Positive(SightRadiusCm)&&Positive(TargetHomeRadiusCm)&&
        Positive(SelfLeashRadiusCm)&&Positive(MemorySeconds)&&Positive(ObservationFreshnessSeconds)&&Positive(HomeArrivalRadiusCm)&&
        ObservationFreshnessSeconds>=SampleIntervalSeconds&&MemorySeconds>=ObservationFreshnessSeconds&&
        HomeArrivalRadiusCm<TargetHomeRadiusCm&&TargetHomeRadiusCm<=SelfLeashRadiusCm;
}
const FAetherNpcPerceptionProfile* FAetherNpcPerceptionDefinitions::Find(const FString& Id) const
{
    const auto* P=Profiles.Find(Id);return bValid&&P&&P->Id.Equals(Id,ESearchCase::CaseSensitive)?P:nullptr;
}
const FAetherNpcPerceptionProfile* FAetherNpcPerceptionDefinitions::ForFighter(const FString& Fighter) const
{
    if(bValid)for(const auto& P:FighterProfiles)if(P.Key.Equals(Fighter,ESearchCase::CaseSensitive))return Find(P.Value);
    return nullptr;
}
FAetherNpcPerceptionDefinitions FAetherNpcPerceptionDefinitions::Parse(const FString& Json)
{
    const auto Fail=[](const TCHAR* Why){FAetherNpcPerceptionDefinitions D;D.Error=Why;return D;};
    TSharedPtr<FJsonObject> Root;double Version=0;
    if(Json.Len()>128*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Fields(*Root,{TEXT("SchemaVersion"),TEXT("Profiles"),TEXT("FighterProfiles")})||
        !Number(*Root,TEXT("SchemaVersion"),Version)||Version!=1)return Fail(TEXT("Unsupported NPC perception schema"));
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Root->TryGetArrayField(TEXT("Profiles"),Rows)||Rows->IsEmpty()||Rows->Num()>64)return Fail(TEXT("Invalid NPC perception profile count"));
    FAetherNpcPerceptionDefinitions D;TSet<FString> Seen;
    for(const auto& Row:*Rows)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherNpcPerceptionProfile P;
        if(!Row->TryGetObject(O)||!O||!O->IsValid()||!Fields(**O,{TEXT("Id"),TEXT("SampleIntervalSeconds"),TEXT("SightRadiusCm"),
            TEXT("TargetHomeRadiusCm"),TEXT("SelfLeashRadiusCm"),TEXT("MemorySeconds"),TEXT("ObservationFreshnessSeconds"),TEXT("HomeArrivalRadiusCm")})||
            !(*O)->HasTypedField<EJson::String>(TEXT("Id"))||!(*O)->TryGetStringField(TEXT("Id"),P.Id)||!Number(**O,TEXT("SampleIntervalSeconds"),P.SampleIntervalSeconds)||
            !Number(**O,TEXT("SightRadiusCm"),P.SightRadiusCm)||!Number(**O,TEXT("TargetHomeRadiusCm"),P.TargetHomeRadiusCm)||
            !Number(**O,TEXT("SelfLeashRadiusCm"),P.SelfLeashRadiusCm)||!Number(**O,TEXT("MemorySeconds"),P.MemorySeconds)||
            !Number(**O,TEXT("ObservationFreshnessSeconds"),P.ObservationFreshnessSeconds)||!Number(**O,TEXT("HomeArrivalRadiusCm"),P.HomeArrivalRadiusCm)||
            !P.IsValid()||Seen.Contains(P.Id.ToLower()))return Fail(TEXT("Missing/invalid/duplicate NPC perception profile"));
        Seen.Add(P.Id.ToLower());const FString Id=P.Id;D.Profiles.Add(Id,MoveTemp(P));
    }
    const TSharedPtr<FJsonObject>* Bindings=nullptr;
    if(!Root->TryGetObjectField(TEXT("FighterProfiles"),Bindings)||!Bindings||!Bindings->IsValid()||(*Bindings)->Values.IsEmpty()||(*Bindings)->Values.Num()>64)
        return Fail(TEXT("Invalid NPC perception fighter bindings"));
    Seen.Reset();
    for(const auto& Entry:(*Bindings)->Values)
    {
        FString Target;
        if(!ValidId(Entry.Key)||Seen.Contains(Entry.Key.ToLower())||Entry.Value->Type!=EJson::String||!Entry.Value->TryGetString(Target))return Fail(TEXT("Invalid/duplicate NPC perception fighter identity"));
        const auto* P=D.Profiles.Find(Target);
        if(!P||!P->Id.Equals(Target,ESearchCase::CaseSensitive))return Fail(TEXT("Unknown NPC perception profile binding"));
        Seen.Add(Entry.Key.ToLower());D.FighterProfiles.Add(Entry.Key,Target);
    }
    D.bValid=true;return D;
}
const FAetherNpcPerceptionDefinitions& FAetherNpcPerceptionDefinitions::Get()
{
    static const auto D=[]
    {
        FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/NpcPerception.json"))))
        {FAetherNpcPerceptionDefinitions Missing;Missing.Error=TEXT("Missing NpcPerception.json; no default perception policy");return Missing;}
        return Parse(Json);
    }();return D;
}
