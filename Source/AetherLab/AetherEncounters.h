#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AetherEncounters.generated.h"
class AAetherFrontierCharacter;
UENUM()
enum class EAetherEncounterPhase:uint8 { Idle, Front, Channel, Elite, Boss, Succeeded, Failed };
USTRUCT()
struct FAetherEncounterRun
{
    GENERATED_BODY()
    UPROPERTY() FName Definition;
    UPROPERTY() FGuid Instance;
    UPROPERTY() EAetherEncounterPhase Phase=EAetherEncounterPhase::Idle;
    UPROPERTY() int32 Version=0;
    UPROPERTY() int32 Wave=0;
    UPROPERTY() int32 LockedSeats=2;
    UPROPERTY() float PhaseStarted=0;
    UPROPERTY() float Progress=0;
    UPROPERTY() TArray<FString> Participants;
    UPROPERTY() TArray<FString> Settled;
};
USTRUCT()
struct FAetherCamp
{
 GENERATED_BODY()
 UPROPERTY() FName Definition;
 UPROPERTY() FGuid Instance;
 UPROPERTY() TArray<TObjectPtr<AAetherFrontierCharacter>> Enemies;
 float ClearedAt=0;
 bool bSpawned=false;
 bool bRewardCreated=false;
};
UCLASS()
class AAetherEncounterDirector : public AActor
{
    GENERATED_BODY()
public:
    AAetherEncounterDirector();
    UPROPERTY(Replicated) FAetherEncounterRun Abbey;
    UPROPERTY(Replicated) FAetherEncounterRun Relay;
    UPROPERTY() TArray<TObjectPtr<AAetherFrontierCharacter>> AbbeyEnemies;
    UPROPERTY() TArray<TObjectPtr<AAetherFrontierCharacter>> RelayEnemies;
    UPROPERTY() TObjectPtr<AAetherFrontierCharacter> AbbeyChanneler;
    UPROPERTY() TObjectPtr<AAetherFrontierCharacter> RelayChanneler;
    uint64 AbbeyChannelDamage=0,RelayChannelDamage=0;
    bool IsChanneling(const AAetherFrontierCharacter* C) const {return AbbeyChanneler==C||RelayChanneler==C;}
    float EmptySinceAbbey=0,EmptySinceRelay=0;
    FString Start(AAetherFrontierCharacter* Player,bool Public);
    FString Channel(AAetherFrontierCharacter* Player,bool Companion=false);
    bool Participates(const AAetherFrontierCharacter* Player,FName Encounter) const;
    UPROPERTY() TArray<FAetherCamp> Camps;
    float CampTimer=0;
    void UpdateCamps();
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    void SetPhase(FAetherEncounterRun& Run,EAetherEncounterPhase Phase);
    void SpawnWave(FAetherEncounterRun& Run,TArray<TObjectPtr<AAetherFrontierCharacter>>& Enemies);
    void UpdateRun(FAetherEncounterRun& Run,TArray<TObjectPtr<AAetherFrontierCharacter>>& Enemies,float Dt,float& EmptySince);
    void Settle(FAetherEncounterRun& Run);
};
