#include "Skills/AetherNpcSkillDefinitions.h"
#include "Skills/AetherSkillState.h"
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
bool Id(const FString& S)
{
    if(S.IsEmpty()||S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='.'||C=='_'))return false;
    return true;
}
}
const FAetherNpcSkillLoadout* FAetherNpcSkillDefinitions::Find(const FString& Id) const
{
    const auto* Row=Loadouts.Find(Id);return bValid&&Row&&Row->Id.Equals(Id,ESearchCase::CaseSensitive)?Row:nullptr;
}
const FAetherNpcSkillLoadout* FAetherNpcSkillDefinitions::ForFighter(const FString& Fighter) const
{
    for(const auto& P:FighterLoadouts)if(P.Key.Equals(Fighter,ESearchCase::CaseSensitive))return Find(P.Value);
    return nullptr;
}
FAetherNpcSkillDefinitions FAetherNpcSkillDefinitions::Parse(const FString& Json,const FAetherSkillDefinitionsV10& Skills)
{
    FAetherNpcSkillDefinitions D;
    const auto Fail=[](const TCHAR* Reason){FAetherNpcSkillDefinitions R;R.Error=Reason;return R;};
    FString Why;if(!Skills.Validate(Why))return Fail(TEXT("NPC skills require valid canonical skill definitions"));
    TSharedPtr<FJsonObject> Root;double Version=0;
    if(Json.Len()>128*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Fields(*Root,{TEXT("SchemaVersion"),TEXT("Loadouts"),TEXT("FighterLoadouts")})||
        !Root->TryGetNumberField(TEXT("SchemaVersion"),Version)||Version!=2)return Fail(TEXT("Unsupported NPC skill catalog schema"));
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Root->TryGetArrayField(TEXT("Loadouts"),Rows)||Rows->IsEmpty()||Rows->Num()>64)return Fail(TEXT("Invalid NPC loadout count"));
    for(const auto& V:*Rows)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherNpcSkillLoadout L;
        if(!V->TryGetObject(O)||!O||!O->IsValid()||!Fields(**O,{TEXT("Id"),TEXT("InitialGrants"),TEXT("OffensiveSkills")})||
            !(*O)->TryGetStringField(TEXT("Id"),L.Id)||!Id(L.Id)||D.Loadouts.Contains(L.Id))return Fail(TEXT("Invalid/duplicate NPC loadout"));
        const TArray<TSharedPtr<FJsonValue>>* Grants=nullptr;
        if(!(*O)->TryGetArrayField(TEXT("InitialGrants"),Grants)||Grants->Num()>32)return Fail(TEXT("Invalid NPC grant count"));
        TSet<FString> Seen;TSet<int32> Slots;
        for(const auto& G:*Grants)
        {
            const TSharedPtr<FJsonObject>* Entry=nullptr;FAetherNpcSkillGrant Grant;double Rank=0,Slot=0;
            if(!G->TryGetObject(Entry)||!Entry||!Entry->IsValid()||!Fields(**Entry,{TEXT("SkillId"),TEXT("Rank"),TEXT("Slot")})||
                !(*Entry)->TryGetStringField(TEXT("SkillId"),Grant.SkillId)||!Id(Grant.SkillId)||Seen.Contains(Grant.SkillId)||
                !(*Entry)->TryGetNumberField(TEXT("Rank"),Rank)||!FMath::IsFinite(Rank)||Rank<1||Rank>MAX_int32||FMath::FloorToDouble(Rank)!=Rank||
                !(*Entry)->TryGetNumberField(TEXT("Slot"),Slot)||!FMath::IsFinite(Slot)||Slot< -1||Slot>=FAetherSkillStateV10::HotbarCapacity||FMath::FloorToDouble(Slot)!=Slot)
                return Fail(TEXT("Invalid NPC skill grant"));
            Grant.Rank=int32(Rank);Grant.Slot=int32(Slot);
            const auto* Skill=Skills.Skills.Find(Grant.SkillId);
            if(!Skill||!Skill->SkillId.Equals(Grant.SkillId,ESearchCase::CaseSensitive)||!Skill->bActive||!Skills.Effect(Grant.SkillId,Grant.Rank)||
                (Grant.Slot>=0&&Slots.Contains(Grant.Slot)))return Fail(TEXT("Unknown/inactive NPC skill, rank or duplicate input slot"));
            Seen.Add(Grant.SkillId);if(Grant.Slot>=0)Slots.Add(Grant.Slot);L.InitialGrants.Add(MoveTemp(Grant));
        }
        if(!(*O)->TryGetStringArrayField(TEXT("OffensiveSkills"),L.OffensiveSkills)||L.OffensiveSkills.Num()>32)
            return Fail(TEXT("Missing or invalid NPC offensive strategy"));
        TSet<FString> OffensiveSeen;
        for(const auto& SkillId:L.OffensiveSkills)
        {
            const auto* Skill=Skills.Skills.Find(SkillId);
            if(!Seen.Contains(SkillId)||OffensiveSeen.Contains(SkillId)||!Skill||!Skill->SkillId.Equals(SkillId,ESearchCase::CaseSensitive)||!SupportsOffensiveActorTarget(*Skill))
                return Fail(TEXT("NPC offensive strategy requires unique granted directional hostile-actor skills"));
            OffensiveSeen.Add(SkillId);
        }
        const FString Key=L.Id;D.Loadouts.Add(Key,MoveTemp(L));
    }
    const TSharedPtr<FJsonObject>* Bindings=nullptr;
    if(!Root->TryGetObjectField(TEXT("FighterLoadouts"),Bindings)||!Bindings||!Bindings->IsValid()||(*Bindings)->Values.IsEmpty()||(*Bindings)->Values.Num()>64)
        return Fail(TEXT("Invalid NPC fighter bindings"));
    for(const auto& P:(*Bindings)->Values)
    {
        FString Target;
        if(!Id(P.Key)||!P.Value->TryGetString(Target))return Fail(TEXT("Invalid NPC fighter binding"));
        const auto* L=D.Loadouts.Find(Target);
        if(!L||!L->Id.Equals(Target,ESearchCase::CaseSensitive))return Fail(TEXT("Unknown NPC fighter loadout"));
        D.FighterLoadouts.Add(P.Key,Target);
    }
    D.bValid=true;return D;
}
bool FAetherNpcSkillDefinitions::SupportsOffensiveActorTarget(const FAetherSkillDefinitionV10& Skill)
{
    // 当前只支持朝敌对 Actor 发射火球、霜/雷定向注入。区域/地面/支持技能不得猜测目标语义。
    if(!Skill.bActive||(Skill.Mechanic!=EAetherSkillMechanic::Fire&&Skill.Mechanic!=EAetherSkillMechanic::Frost&&Skill.Mechanic!=EAetherSkillMechanic::Lightning)||Skill.Ranks.IsEmpty())return false;
    for(const auto& Rank:Skill.Ranks)if(!FMath::IsFinite(Rank.RangeCm)||Rank.RangeCm<=0)return false;
    return true;
}
const FAetherNpcSkillDefinitions& FAetherNpcSkillDefinitions::Get()
{
    static const auto D=[]
    {
        FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/NpcSkills.json"))))
        {FAetherNpcSkillDefinitions Missing;Missing.Error=TEXT("Missing NpcSkills.json; no default capability grant");return Missing;}
        return Parse(Json,FAetherSkillDefinitionsV10::Get());
    }();return D;
}
