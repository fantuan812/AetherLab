#include "AI/AetherCompanionSupportDefinitions.h"
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
bool Id(const FString& Value)
{
    if(Value.IsEmpty()||Value.Len()>96)return false;
    for(TCHAR C:Value)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='.'||C=='_'))return false;
    return true;
}
bool Number(const FJsonObject& O,const TCHAR* Key,double& Out)
{return O.HasTypedField<EJson::Number>(Key)&&O.TryGetNumberField(Key,Out)&&FMath::IsFinite(Out);}
bool String(const FJsonObject& O,const TCHAR* Key,FString& Out)
{return O.HasTypedField<EJson::String>(Key)&&O.TryGetStringField(Key,Out);}
bool Choice(const FJsonObject& Parent,const TCHAR* Key,FAetherCompanionSupportSkill& Out,const FJsonObject*& Object)
{
    const TSharedPtr<FJsonObject>* O=nullptr;
    if(!Parent.TryGetObjectField(Key,O)||!O||!O->IsValid()||!(*O)->HasTypedField<EJson::Boolean>(TEXT("Enabled"))||!(*O)->TryGetBoolField(TEXT("Enabled"),Out.bEnabled))return false;
    Object=O->Get();
    if(!Out.bEnabled)return Fields(*Object,{TEXT("Enabled")});
    return String(*Object,TEXT("SkillId"),Out.SkillId)&&Id(Out.SkillId);
}
}
const FAetherCompanionSupportSkill& FAetherCompanionSupportProfile::Skill(EAetherCompanionSupportPurpose Purpose) const
{
    switch(Purpose){case EAetherCompanionSupportPurpose::SelfHealing:return SelfHealing;case EAetherCompanionSupportPurpose::FriendlyHealing:return FriendlyHealing;default:return Cooling;}
}
bool FAetherCompanionSupportProfile::ValidateSkills(const FAetherSkillDefinitionsV10& Skills,const FAetherBuffDefinitions& Buffs,FString& Reason) const
{
    for(const auto Purpose:{EAetherCompanionSupportPurpose::SelfHealing,EAetherCompanionSupportPurpose::FriendlyHealing,EAetherCompanionSupportPurpose::Cooling})
    {
        const auto& Choice=Skill(Purpose);if(!Choice.bEnabled)continue;
        const auto* D=Skills.Skills.Find(Choice.SkillId);
        const auto Mechanic=Purpose==EAetherCompanionSupportPurpose::SelfHealing?EAetherSkillMechanic::SelfBuff:
            Purpose==EAetherCompanionSupportPurpose::FriendlyHealing?EAetherSkillMechanic::FriendlyTargetBuff:EAetherSkillMechanic::Water;
        if(!D||!D->SkillId.Equals(Choice.SkillId,ESearchCase::CaseSensitive)||!D->bActive||D->Mechanic!=Mechanic||D->Ranks.IsEmpty())
        {Reason=TEXT("Support skill identity/target mechanic is unsupported: ")+Choice.SkillId;return false;}
        for(const auto& Rank:D->Ranks)
        {
            if(Purpose==EAetherCompanionSupportPurpose::Cooling)
            {
                if(Rank.WaterKg<=0||Rank.HeatJ>0||Rank.ElectricalJ!=0){Reason=TEXT("Cooling requires the formal water delivery mechanic: ")+Choice.SkillId;return false;}
                continue;
            }
            const auto* B=Buffs.bValid?Buffs.Buffs.Find(Rank.BuffId):nullptr;
            if(!B||B->Period<=0||B->Operations.IsEmpty()){Reason=TEXT("Support skill needs a periodic healing effect: ")+Choice.SkillId;return false;}
            for(const auto& O:B->Operations)if(O.Kind!=EAetherBuffOperation::Heal||!O.Id.Equals(TEXT("Health"),ESearchCase::CaseSensitive))
            {Reason=TEXT("Support skill effect is not Health-only healing: ")+Choice.SkillId;return false;}
        }
    }
    Reason.Reset();return true;
}
const FAetherCompanionSupportProfile* FAetherCompanionSupportDefinitions::ForLoadout(const FString& Loadout) const
{
    if(!bValid)return nullptr;
    for(const auto& Pair:LoadoutProfiles)if(Pair.Key.Equals(Loadout,ESearchCase::CaseSensitive))
    {const auto* P=Profiles.Find(Pair.Value);return P&&P->Id.Equals(Pair.Value,ESearchCase::CaseSensitive)?P:nullptr;}
    return nullptr;
}
FAetherCompanionSupportDefinitions FAetherCompanionSupportDefinitions::Parse(const FString& Json)
{
    const auto Fail=[](const TCHAR* Why){FAetherCompanionSupportDefinitions D;D.Error=Why;return D;};
    TSharedPtr<FJsonObject> Root;double Version=0;
    if(Json.Len()>128*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Fields(*Root,{TEXT("SchemaVersion"),TEXT("Profiles"),TEXT("LoadoutProfiles")})||!Number(*Root,TEXT("SchemaVersion"),Version)||Version!=1)
        return Fail(TEXT("Unsupported companion support schema"));
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Root->TryGetArrayField(TEXT("Profiles"),Rows)||Rows->IsEmpty()||Rows->Num()>64)return Fail(TEXT("Invalid support profile count"));
    FAetherCompanionSupportDefinitions D;TSet<FString> Seen;
    for(const auto& Row:*Rows)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherCompanionSupportProfile P;const FJsonObject *Self=nullptr,*Friendly=nullptr,*Cooling=nullptr;
        if(!Row->TryGetObject(O)||!O||!O->IsValid()||!Fields(**O,{TEXT("Id"),TEXT("SampleIntervalSeconds"),TEXT("PatientHealthRatioBelow"),TEXT("SelfHealing"),TEXT("FriendlyHealing"),TEXT("Cooling")})||
            !String(**O,TEXT("Id"),P.Id)||!Id(P.Id)||Seen.Contains(P.Id.ToLower())||!Number(**O,TEXT("SampleIntervalSeconds"),P.SampleIntervalSeconds)||P.SampleIntervalSeconds<=0||
            !Number(**O,TEXT("PatientHealthRatioBelow"),P.PatientHealthRatioBelow)||P.PatientHealthRatioBelow<=0||P.PatientHealthRatioBelow>1||
            !Choice(**O,TEXT("SelfHealing"),P.SelfHealing,Self)||!Choice(**O,TEXT("FriendlyHealing"),P.FriendlyHealing,Friendly)||!Choice(**O,TEXT("Cooling"),P.Cooling,Cooling))
            return Fail(TEXT("Missing/invalid/duplicate companion support policy"));
        for(const auto* Option:{Self,Friendly})if(Option->GetBoolField(TEXT("Enabled"))&&!Fields(*Option,{TEXT("Enabled"),TEXT("SkillId")}))return Fail(TEXT("Unexpected support skill fields"));
        if(P.Cooling.bEnabled)
        {
            FString Priority;
            if(!Fields(*Cooling,{TEXT("Enabled"),TEXT("SkillId"),TEXT("AboveTemperatureC"),TEXT("Priority")})||!Number(*Cooling,TEXT("AboveTemperatureC"),P.CoolingAboveTemperatureC)||!String(*Cooling,TEXT("Priority"),Priority))
                return Fail(TEXT("Missing cooling policy/priority"));
            if(Priority==TEXT("BeforeHealing"))P.CoolingPriority=EAetherCompanionCoolingPriority::BeforeHealing;
            else if(Priority==TEXT("AfterHealing"))P.CoolingPriority=EAetherCompanionCoolingPriority::AfterHealing;
            else return Fail(TEXT("Unsupported cooling priority"));
        }
        Seen.Add(P.Id.ToLower());const FString Key=P.Id;D.Profiles.Add(Key,MoveTemp(P));
    }
    const TSharedPtr<FJsonObject>* Bindings=nullptr;
    if(!Root->TryGetObjectField(TEXT("LoadoutProfiles"),Bindings)||!Bindings||!Bindings->IsValid()||(*Bindings)->Values.IsEmpty()||(*Bindings)->Values.Num()>64)return Fail(TEXT("Invalid support loadout bindings"));
    Seen.Reset();
    for(const auto& Pair:(*Bindings)->Values)
    {
        FString Target;
        if(!Id(Pair.Key)||Seen.Contains(Pair.Key.ToLower())||Pair.Value->Type!=EJson::String||!Pair.Value->TryGetString(Target))return Fail(TEXT("Invalid support loadout identity"));
        const auto* P=D.Profiles.Find(Target);if(!P||!P->Id.Equals(Target,ESearchCase::CaseSensitive))return Fail(TEXT("Unknown support profile"));
        Seen.Add(Pair.Key.ToLower());D.LoadoutProfiles.Add(Pair.Key,Target);
    }
    D.bValid=true;return D;
}
const FAetherCompanionSupportDefinitions& FAetherCompanionSupportDefinitions::Get()
{
    static const auto D=[]
    {
        FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/CompanionSupport.json"))))
        {FAetherCompanionSupportDefinitions Missing;Missing.Error=TEXT("Missing CompanionSupport.json; support decisions disabled");return Missing;}
        return Parse(Json);
    }();return D;
}
