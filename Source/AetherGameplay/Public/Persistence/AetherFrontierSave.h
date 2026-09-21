#pragma once
#include "CoreMinimal.h"
#include "ReactiveTypes.h"
#include "GameFramework/SaveGame.h"
#include "Persistence/AetherProfile.h"
#include "Quests/AetherQuestRuntime.h"
#include "World/AetherEncounters.h"
#include "Interaction/AetherServices.h"
#include "AetherFrontierSave.generated.h"

class AAetherFrontierProp;
class AAetherFrontierCharacter;
class UAetherPhysicsDamageComponent;
class UAetherTraversalComponent;
class UPhysicsHandleComponent;
class UInputMappingContext;
class UInputAction;
class UAetherFrontierPanel;
struct FAetherWorldPlacement;

// 保留 v9 反射类型与字段，以便显式迁移前仍能读取冻结夹具。
USTRUCT()
struct FAetherWorldLoot
{
 GENERATED_BODY()
 UPROPERTY() FGuid ClaimId;
 UPROPERTY() FVector Location=FVector::ZeroVector;
 UPROPERTY() FName Definition="Material";
 UPROPERTY() int32 Count=1;
 UPROPERTY() TMap<FName,int32> Items;
 UPROPERTY() FString ClaimedBy;
};
USTRUCT()
struct FAetherCampReceipt
{
 GENERATED_BODY()
 UPROPERTY() FName Definition;
 UPROPERTY() FGuid Instance;
 UPROPERTY() int64 RespawnAfterUtc=0;
};
UCLASS()
class UAetherFrontierSave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 Version = 5;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() TArray<FAetherProfile> Profiles;
    UPROPERTY() TArray<FAetherWorldLoot> Loot;
    UPROPERTY() TArray<FAetherCampReceipt> CampReceipts;
    UPROPERTY() TArray<FAetherWorldServiceReceipt> ServiceReceipts;
    UPROPERTY() FAetherWorldFacts WorldFacts;
    bool ValidateWorldLedger(const FAetherRules& Rules=FAetherRules::Get()) const;
    UPROPERTY() bool bSupplyRestored = false;
    UPROPERTY() bool bWorkshopRestored = false;
    UPROPERTY() bool bBridgeReleased = false;
    UPROPERTY() bool bPowerOn = true;
    UPROPERTY() double RainKgPerM2Sec = 0;
    UPROPERTY() double AmbientTemperatureC = 20;
    UPROPERTY() FVector WindMPerSec = FVector::ZeroVector;
    UPROPERTY() TArray<FReactiveSaveRecord> World;
    UPROPERTY() FAetherEncounterRun Abbey;
    UPROPERTY() FAetherEncounterRun Relay;
};
