#pragma once
#include "CoreMinimal.h"
struct FAetherItemRule
{
    int32 MaxStack=0,Buy=0,Sell=0;
    bool bPlayerEquippable=false,bRemovable=true,bSellable=false;
    FName EquipmentId,Slot,UseId;
    TArray<FName> OccupiedSlots;
};
enum class EAetherObjectiveScope : uint8 { Personal,World,Party,Daily };
struct FAetherQuestRule
{
    FName Id;FString Title;TArray<FName> Prerequisites,Objectives;
    int32 Gold=0,Experience=0;TMap<FName,int32> Items;
    bool bAutoClaim=true,bBindInn=false;
};
struct FAetherObjectiveRule
{
    FString Label,Hint;FName Anchor;FVector Position=FVector::ZeroVector;
    EAetherObjectiveScope Scope=EAetherObjectiveScope::Personal;
    bool bRetroactive=false,bInspectableFire=false;
    TArray<FName> FactSources;
};
struct FAetherDailyRule { FName Id,Service,QuestGate;TMap<FName,int32> Consume,Reward;TArray<FName> Facts;int32 Gold=0;bool bPersonalFires=false; };
struct FAetherUseRule { double Health=0,Mana=0,Stamina=0,Cooldown=3,SafeSeconds=0; };
struct FAetherEncounterRule { FVector Center=FVector::ZeroVector;TArray<uint8> Types;float RespawnSeconds=0;FName LootTable="Camp"; };
struct FAetherRules
{
    TMap<FName,FAetherItemRule> Items;
    TArray<FAetherQuestRule> Quests;
    TMap<FName,FAetherObjectiveRule> Objectives;
    TMap<FName,FAetherEncounterRule> Encounters;
    int32 SchemaVersion=1,InventoryCapacity=32;
    TMap<FName,FAetherUseRule> Uses;
    TMap<FName,TArray<FName>> Shops;
    TMap<FName,TMap<FName,int32>> LootTables;
    TArray<FAetherDailyRule> Dailies;
    TMap<FName,TArray<FName>> InteractionRequirements;
    TMap<FName,uint8> SpellUnlocks;
    double PourKg=.5,PourRangeCm=600;
    bool bValid=false;FString Error;
    const FAetherQuestRule* Quest(FName Id) const;
    static FAetherRules Parse(const FString& Text);
    static const FAetherRules& Get();
};
