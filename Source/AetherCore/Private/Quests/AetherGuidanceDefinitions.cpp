#include "Quests/AetherGuidanceDefinitions.h"
#include "Definitions/AetherRules.h"
#include "Definitions/AetherWorldDefinition.h"
#include "Skills/AetherNpcSkillDefinitions.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <initializer_list>
namespace
{
bool Fields(const FJsonObject& O,std::initializer_list<const TCHAR*> Keys)
{
    if(O.Values.Num()!=int32(Keys.size()))return false;
    for(const auto& P:O.Values){bool Found=false;for(const auto* K:Keys)Found|=P.Key.Equals(K,ESearchCase::CaseSensitive);if(!Found)return false;}return true;
}
bool Text(const FJsonObject& O,const TCHAR* Key,FString& Out,bool Empty=false)
{return O.TryGetStringField(Key,Out)&&Out.Len()<=512&&(Empty||!Out.TrimStartAndEnd().IsEmpty());}
bool Names(const FJsonObject& O,const TCHAR* Key,TArray<FString>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!O.TryGetArrayField(Key,A)||A->Num()>32)return false;
    TSet<FString> Seen;for(const auto& V:*A){FString Id;if(!V->TryGetString(Id)||Id.IsEmpty()||Id.Len()>96||Seen.Contains(Id))return false;Seen.Add(Id);Out.Add(Id);}return true;
}
}
FAetherGuidanceDefinitions FAetherGuidanceDefinitions::Parse(const FString& Json,const FAetherRules& Rules,const FAetherWorldDefinitions& World,
    const FAetherSkillDefinitionsV10& Skills,const FAetherNpcSkillDefinitions& NpcSkills)
{
    const auto Fail=[](const TCHAR* Why){FAetherGuidanceDefinitions D;D.Error=Why;return D;};
    FString SkillReason;
    if(!Rules.bValid||!World.bValid||!NpcSkills.bValid||!Skills.Validate(SkillReason))return Fail(TEXT("Guidance requires canonical rules/world/NPC definitions"));
    TSharedPtr<FJsonObject> Root;double Schema=0;FAetherGuidanceDefinitions D;
    if(Json.Len()>128*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Fields(*Root,{TEXT("SchemaVersion"),TEXT("Messages"),TEXT("Preparations"),TEXT("DynamicObjectives"),TEXT("EncounterPhases"),TEXT("Completion")})||
        !Root->TryGetNumberField(TEXT("SchemaVersion"),Schema)||Schema!=1)return Fail(TEXT("Unsupported guidance schema"));
    const auto Anchor=[&](const FString& Id){const auto* A=World.Find(FName(*Id));return A&&A->Id.ToString().Equals(Id,ESearchCase::CaseSensitive);};
    const auto Objective=[&](const FString& Id){for(const auto& P:Rules.Objectives)if(P.Key.ToString().Equals(Id,ESearchCase::CaseSensitive))return true;return false;};
    const TSharedPtr<FJsonObject>* M=nullptr;
    if(!Root->TryGetObjectField(TEXT("Messages"),M)||!M||!M->IsValid()||
        !Fields(**M,{TEXT("LoadingTitle"),TEXT("LoadingHint"),TEXT("RewardReadyLabel"),TEXT("RewardReadyHint"),TEXT("PendingRewardHint")})||
        !Text(**M,TEXT("LoadingTitle"),D.LoadingTitle)||!Text(**M,TEXT("LoadingHint"),D.LoadingHint)||
        !Text(**M,TEXT("RewardReadyLabel"),D.RewardReadyLabel)||!Text(**M,TEXT("RewardReadyHint"),D.RewardReadyHint)||
        !Text(**M,TEXT("PendingRewardHint"),D.PendingRewardHint))return Fail(TEXT("Invalid guidance messages"));
    const TSharedPtr<FJsonObject>* C=nullptr;
    if(!Root->TryGetObjectField(TEXT("Completion"),C)||!C||!C->IsValid()||!Fields(**C,{TEXT("Title"),TEXT("Label"),TEXT("Hint"),TEXT("AnchorId")})||
        !Text(**C,TEXT("Title"),D.Completion.Title)||!Text(**C,TEXT("Label"),D.Completion.Label)||!Text(**C,TEXT("Hint"),D.Completion.Hint)||
        !Text(**C,TEXT("AnchorId"),D.Completion.AnchorId)||!Anchor(D.Completion.AnchorId))return Fail(TEXT("Invalid completion anchor/presentation"));
    const auto* CompletionAnchor=World.Find(FName(*D.Completion.AnchorId));
    bool CompletionService=false;for(const auto& Daily:Rules.Dailies)CompletionService|=CompletionAnchor&&Daily.Service==CompletionAnchor->Service;
    if(!CompletionService)return Fail(TEXT("Completion anchor is not a declared daily service"));
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Root->TryGetArrayField(TEXT("DynamicObjectives"),Rows)||Rows->Num()>64)return Fail(TEXT("Invalid dynamic guidance count"));
    for(const auto& V:*Rows)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherDynamicObjective R;FString Kind;
        if(!V->TryGetObject(O)||!O||!O->IsValid()||!Fields(**O,{TEXT("ObjectiveId"),TEXT("Kind"),TEXT("SelectorId")})||
            !Text(**O,TEXT("ObjectiveId"),R.ObjectiveId)||!Objective(R.ObjectiveId)||D.DynamicObjectives.Contains(R.ObjectiveId)||
            !Text(**O,TEXT("Kind"),Kind)||!Text(**O,TEXT("SelectorId"),R.SelectorId))return Fail(TEXT("Invalid/duplicate dynamic objective"));
        if(Kind.Equals(TEXT("OwnedService"),ESearchCase::CaseSensitive))
        {
            R.Kind=EAetherGuidanceTargetKind::OwnedService;
            // Only the registered training spawner or a placed service declares a usable service identity.
            bool Known=Rules.PersonalTraining.FireObjectiveId.ToString().Equals(R.SelectorId,ESearchCase::CaseSensitive);for(const auto& A:World.Objects)Known|=A.Service.ToString().Equals(R.SelectorId,ESearchCase::CaseSensitive);
            if(!Known)return Fail(TEXT("Unknown owned service selector"));
        }
        else if(Kind.Equals(TEXT("OwnedFighter"),ESearchCase::CaseSensitive))
        {R.Kind=EAetherGuidanceTargetKind::OwnedFighter;if(!NpcSkills.ForFighter(R.SelectorId))return Fail(TEXT("Unknown owned fighter selector"));}
        else return Fail(TEXT("Unsupported dynamic guidance selector"));
        const FString Key=R.ObjectiveId;D.DynamicObjectives.Add(Key,MoveTemp(R));
    }
    if(!Root->TryGetArrayField(TEXT("Preparations"),Rows)||Rows->Num()>64)return Fail(TEXT("Invalid preparation count"));
    TSet<FString> Quests;
    for(const auto& V:*Rows)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherGuidancePreparation R;
        if(!V->TryGetObject(O)||!O||!O->IsValid()||!Fields(**O,{TEXT("QuestId"),TEXT("AnchorId"),TEXT("RequiredPermanentSkills"),TEXT("RequiredLiveObjectives"),TEXT("Label"),TEXT("Hint")})||
            !Text(**O,TEXT("QuestId"),R.QuestId)||!Text(**O,TEXT("AnchorId"),R.AnchorId)||!Anchor(R.AnchorId)||Quests.Contains(R.QuestId)||
            !Text(**O,TEXT("Label"),R.Label)||!Text(**O,TEXT("Hint"),R.Hint)||
            !Names(**O,TEXT("RequiredPermanentSkills"),R.RequiredPermanentSkills)||!Names(**O,TEXT("RequiredLiveObjectives"),R.RequiredLiveObjectives))return Fail(TEXT("Invalid preparation"));
        const auto* Q=Rules.Quest(FName(*R.QuestId));if(!Q||!Q->Id.ToString().Equals(R.QuestId,ESearchCase::CaseSensitive))return Fail(TEXT("Unknown preparation quest"));
        for(const auto& Id:R.RequiredPermanentSkills){const auto* S=Skills.Skills.Find(Id);if(!S||!S->SkillId.Equals(Id,ESearchCase::CaseSensitive)||!S->bActive)return Fail(TEXT("Unknown preparation skill"));}
        for(const auto& Id:R.RequiredLiveObjectives)
        {
            const auto* Dynamic=D.DynamicObjectives.Find(Id);
            if(!Q->Objectives.ContainsByPredicate([&](FName O){return O.ToString().Equals(Id,ESearchCase::CaseSensitive);})||
                !Dynamic||!Dynamic->ObjectiveId.Equals(Id,ESearchCase::CaseSensitive))return Fail(TEXT("Preparation refers to an undefined or unrelated dynamic objective"));
        }
        Quests.Add(R.QuestId);D.Preparations.Add(MoveTemp(R));
    }
    if(!Root->TryGetArrayField(TEXT("EncounterPhases"),Rows)||Rows->Num()>64)return Fail(TEXT("Invalid encounter guidance count"));
    TSet<FString> Phases;
    for(const auto& V:*Rows)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherEncounterGuidance R;
        if(!V->TryGetObject(O)||!O||!O->IsValid()||!Fields(**O,{TEXT("EncounterId"),TEXT("Phase"),TEXT("AnchorId"),TEXT("EncounterDefinitionId"),TEXT("Label"),TEXT("Hint")})||
            !Text(**O,TEXT("EncounterId"),R.EncounterId)||!Text(**O,TEXT("Phase"),R.Phase)||!Text(**O,TEXT("AnchorId"),R.AnchorId,true)||
            !Text(**O,TEXT("EncounterDefinitionId"),R.EncounterDefinitionId,true)||!Text(**O,TEXT("Label"),R.Label)||!Text(**O,TEXT("Hint"),R.Hint)||
            R.AnchorId.IsEmpty()==R.EncounterDefinitionId.IsEmpty())return Fail(TEXT("Invalid/ambiguous encounter guidance target"));
        bool Activity=false;for(const auto& P:Rules.ActivityRewards)Activity|=P.Key.ToString().Equals(R.EncounterId,ESearchCase::CaseSensitive)&&!P.Value.Objective.IsNone();
        if(!Activity||!(R.Phase.Equals(TEXT("Front"),ESearchCase::CaseSensitive)||R.Phase.Equals(TEXT("Channel"),ESearchCase::CaseSensitive)||
            R.Phase.Equals(TEXT("Elite"),ESearchCase::CaseSensitive)||R.Phase.Equals(TEXT("Boss"),ESearchCase::CaseSensitive)))return Fail(TEXT("Unknown encounter identity/active phase"));
        if(!R.AnchorId.IsEmpty()){if(!Anchor(R.AnchorId))return Fail(TEXT("Unknown encounter guidance anchor"));}
        else {bool Known=false;for(const auto& P:Rules.Encounters)Known|=P.Key.ToString().Equals(R.EncounterDefinitionId,ESearchCase::CaseSensitive);if(!Known)return Fail(TEXT("Unknown encounter center definition"));}
        const FString Key=R.EncounterId+TEXT(".")+R.Phase;if(Phases.Contains(Key))return Fail(TEXT("Duplicate encounter phase guidance"));
        Phases.Add(Key);D.EncounterPhases.Add(MoveTemp(R));
    }
    D.bValid=true;return D;
}
