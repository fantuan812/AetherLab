#pragma once
#include "CoreMinimal.h"
struct FAetherRules;
struct FAetherWorldDefinitions;
struct FAetherSkillDefinitionsV10;
struct FAetherNpcSkillDefinitions;
struct FAetherGuidancePreparation
{
    FString QuestId,AnchorId,Label,Hint;
    TArray<FString> RequiredPermanentSkills,RequiredLiveObjectives;
};
enum class EAetherGuidanceTargetKind:uint8 {OwnedService,OwnedFighter};
struct FAetherDynamicObjective
{
    FString ObjectiveId,SelectorId;
    EAetherGuidanceTargetKind Kind=EAetherGuidanceTargetKind::OwnedService;
};
struct FAetherEncounterGuidance
{
    FString EncounterId,Phase,AnchorId,EncounterDefinitionId,Label,Hint;
};
struct FAetherGuidanceCompletion {FString Title,Label,Hint,AnchorId;};
struct AETHERCORE_API FAetherGuidanceDefinitions
{
    TArray<FAetherGuidancePreparation> Preparations;
    TMap<FString,FAetherDynamicObjective> DynamicObjectives;
    TArray<FAetherEncounterGuidance> EncounterPhases;
    FAetherGuidanceCompletion Completion;
    FString LoadingTitle,LoadingHint,RewardReadyLabel,RewardReadyHint,PendingRewardHint;
    bool bValid=false;
    FString Error;
    static FAetherGuidanceDefinitions Parse(const FString& Json,const FAetherRules& Rules,const FAetherWorldDefinitions& World,
        const FAetherSkillDefinitionsV10& Skills,const FAetherNpcSkillDefinitions& NpcSkills);
};
