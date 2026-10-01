#include "AI/AetherNpcMeleeDefinitions.h"
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
bool ValidId(const FString& S)
{
    if(S.IsEmpty()||S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='.'||C=='_'))return false;
    return true;
}
bool Number(const FJsonObject& O,const TCHAR* Key,double& Out)
{return O.HasTypedField<EJson::Number>(Key)&&O.TryGetNumberField(Key,Out)&&FMath::IsFinite(Out)&&FMath::Abs(Out)<=MAX_flt;}
bool String(const FJsonObject& O,const TCHAR* Key,FString& Out)
{return O.HasTypedField<EJson::String>(Key)&&O.TryGetStringField(Key,Out);}
}
bool FAetherNpcMeleeProfile::IsValid() const
{
    if(!ValidId(Id)||(!AttackId.Equals(TEXT("Light"),ESearchCase::CaseSensitive)&&!AttackId.Equals(TEXT("Heavy"),ESearchCase::CaseSensitive))||!FMath::IsFinite(StartRangeMarginCm)||StartRangeMarginCm<0||StartRangeMarginCm>MAX_flt||
        !FMath::IsFinite(MotionSpeedCmPerSecond)||MotionSpeedCmPerSecond<0||MotionSpeedCmPerSecond>MAX_flt||HealthBands.IsEmpty()||HealthBands.Num()>16)return false;
    if(Motion==EAetherNpcAttackMotion::None){if(MotionSpeedCmPerSecond!=0)return false;}
    else if(Motion!=EAetherNpcAttackMotion::ForwardDuringActive||MotionSpeedCmPerSecond<=0)return false;
    double Previous=0;
    for(const auto& B:HealthBands)
    {
        if(!FMath::IsFinite(B.MaxHealthFraction)||B.MaxHealthFraction<=Previous||B.MaxHealthFraction>1||
            !FMath::IsFinite(B.TelegraphSeconds)||B.TelegraphSeconds<0||B.TelegraphSeconds>MAX_flt)return false;
        Previous=B.MaxHealthFraction;
    }
    return Previous==1; // 每个合法[0,1]比例恰好归属一个显式区间，不补最后一行默认值。
}
bool FAetherNpcMeleeProfile::TelegraphFor(double Ratio,double& Out) const
{
    Out=0;if(!IsValid()||!FMath::IsFinite(Ratio)||Ratio<0||Ratio>1)return false;
    for(const auto& B:HealthBands)if(Ratio<B.MaxHealthFraction||B.MaxHealthFraction==1){Out=B.TelegraphSeconds;return true;}
    return false;
}
const FAetherNpcMeleeProfile* FAetherNpcMeleeDefinitions::Find(const FString& Name) const
{const auto* P=Profiles.Find(Name);return bValid&&P&&P->Id.Equals(Name,ESearchCase::CaseSensitive)?P:nullptr;}
const FAetherNpcMeleeProfile* FAetherNpcMeleeDefinitions::ForFighter(const FString& Fighter) const
{if(bValid)for(const auto& P:FighterProfiles)if(P.Key.Equals(Fighter,ESearchCase::CaseSensitive))return Find(P.Value);return nullptr;}
FAetherNpcMeleeDefinitions FAetherNpcMeleeDefinitions::Parse(const FString& Json)
{
    const auto Fail=[](const TCHAR* Why){FAetherNpcMeleeDefinitions D;D.Error=Why;return D;};
    TSharedPtr<FJsonObject> Root;double Version=0;
    if(Json.Len()>128*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Fields(*Root,{TEXT("SchemaVersion"),TEXT("Profiles"),TEXT("FighterProfiles")})||!Number(*Root,TEXT("SchemaVersion"),Version)||Version!=1)
        return Fail(TEXT("Unsupported NPC melee schema"));
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Root->TryGetArrayField(TEXT("Profiles"),Rows)||Rows->IsEmpty()||Rows->Num()>64)return Fail(TEXT("Invalid NPC melee profile count"));
    FAetherNpcMeleeDefinitions D;TSet<FString> Seen;
    for(const auto& Row:*Rows)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherNpcMeleeProfile P;
        if(!Row->TryGetObject(O)||!O||!O->IsValid()||!Fields(**O,{TEXT("Id"),TEXT("AttackId"),TEXT("GuardWhileApproaching"),TEXT("StartRangeMarginCm"),TEXT("HealthBands"),TEXT("Motion")})||
            !String(**O,TEXT("Id"),P.Id)||!String(**O,TEXT("AttackId"),P.AttackId)||!(*O)->HasTypedField<EJson::Boolean>(TEXT("GuardWhileApproaching"))||
            !(*O)->TryGetBoolField(TEXT("GuardWhileApproaching"),P.bGuardWhileApproaching)||!Number(**O,TEXT("StartRangeMarginCm"),P.StartRangeMarginCm))
            return Fail(TEXT("Missing/invalid NPC melee profile fields"));
        const TArray<TSharedPtr<FJsonValue>>* Bands=nullptr;
        if(!(*O)->TryGetArrayField(TEXT("HealthBands"),Bands)||Bands->IsEmpty()||Bands->Num()>16)return Fail(TEXT("Invalid NPC health partitions"));
        for(const auto& Band:*Bands)
        {
            const TSharedPtr<FJsonObject>* B=nullptr;FAetherNpcMeleeHealthBand Value;
            if(!Band->TryGetObject(B)||!B||!B->IsValid()||!Fields(**B,{TEXT("MaxHealthFraction"),TEXT("TelegraphSeconds")})||
                !Number(**B,TEXT("MaxHealthFraction"),Value.MaxHealthFraction)||!Number(**B,TEXT("TelegraphSeconds"),Value.TelegraphSeconds))
                return Fail(TEXT("Invalid NPC health band"));
            P.HealthBands.Add(Value);
        }
        const TSharedPtr<FJsonObject>* Motion=nullptr;FString Kind;
        if(!(*O)->TryGetObjectField(TEXT("Motion"),Motion)||!Motion||!Motion->IsValid()||!Fields(**Motion,{TEXT("Policy"),TEXT("SpeedCmPerSecond")})||
            !String(**Motion,TEXT("Policy"),Kind)||!Number(**Motion,TEXT("SpeedCmPerSecond"),P.MotionSpeedCmPerSecond))return Fail(TEXT("Invalid NPC melee motion"));
        if(Kind.Equals(TEXT("None"),ESearchCase::CaseSensitive))P.Motion=EAetherNpcAttackMotion::None;
        else if(Kind.Equals(TEXT("ForwardDuringActive"),ESearchCase::CaseSensitive))P.Motion=EAetherNpcAttackMotion::ForwardDuringActive;
        else return Fail(TEXT("Unknown NPC melee motion executor"));
        if(!P.IsValid()||Seen.Contains(P.Id.ToLower()))return Fail(TEXT("Invalid/duplicate NPC melee profile"));
        Seen.Add(P.Id.ToLower());const FString Name=P.Id;D.Profiles.Add(Name,MoveTemp(P));
    }
    const TSharedPtr<FJsonObject>* Bindings=nullptr;
    if(!Root->TryGetObjectField(TEXT("FighterProfiles"),Bindings)||!Bindings||!Bindings->IsValid()||(*Bindings)->Values.IsEmpty()||(*Bindings)->Values.Num()>64)
        return Fail(TEXT("Invalid NPC melee bindings"));
    Seen.Reset();
    for(const auto& B:(*Bindings)->Values)
    {
        FString Name;
        if(!ValidId(B.Key)||Seen.Contains(B.Key.ToLower())||B.Value->Type!=EJson::String||!B.Value->TryGetString(Name))return Fail(TEXT("Invalid NPC melee fighter identity"));
        const auto* P=D.Profiles.Find(Name);if(!P||!P->Id.Equals(Name,ESearchCase::CaseSensitive))return Fail(TEXT("Unknown NPC melee profile binding"));
        Seen.Add(B.Key.ToLower());D.FighterProfiles.Add(B.Key,Name);
    }
    D.bValid=true;return D;
}
const FAetherNpcMeleeDefinitions& FAetherNpcMeleeDefinitions::Get()
{
    static const auto D=[]
    {
        FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/NpcMelee.json"))))
        {FAetherNpcMeleeDefinitions Missing;Missing.Error=TEXT("Missing NpcMelee.json; no default melee policy");return Missing;}
        return Parse(Json);
    }();return D;
}
