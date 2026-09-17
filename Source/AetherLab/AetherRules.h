#pragma once
#include "CoreMinimal.h"
struct FAetherItemRule { int32 MaxStack=0; int32 Buy=0; int32 Sell=0; };
struct FAetherQuestRule { FName Id; FString Title; TArray<int32> Prerequisites; TArray<FName> Objectives; int32 Gold=0; int32 Experience=0; TMap<FName,int32> Items; };
struct FAetherObjectiveRule { FString Label,Hint;FName Anchor;FVector Position=FVector::ZeroVector; };
struct FAetherEncounterRule { FVector Center=FVector::ZeroVector;TArray<uint8> Types;float RespawnSeconds=0; };
// Immutable content definitions. No live player state is stored here.
struct FAetherRules
{
    TMap<FName,FAetherItemRule> Items;
    TArray<FAetherQuestRule> Quests;
    TMap<FName,FAetherObjectiveRule> Objectives;
    TMap<FName,FAetherEncounterRule> Encounters;
    bool bValid=false;
    FString Error;
    static const FAetherRules& Get();
};
