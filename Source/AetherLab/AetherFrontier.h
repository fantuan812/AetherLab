#pragma once
#include "CoreMinimal.h"
#include "AetherAdventure.h"
#include "AetherProgression.h"
#include "ReactiveMechanismComponent.h"
#include "AetherEncounters.h"
#include "AetherGuide.h"
#include "AetherServices.h"
#include "AetherFrontier.generated.h"
class UPhysicsHandleComponent;
class UInputMappingContext;
class UInputAction;
class UAetherFrontierPanel;
class USkeletalMeshComponent;

UCLASS()
class AAetherFrontierProp : public AAetherWorldObject
{
    GENERATED_BODY()
public:
    AAetherFrontierProp();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UReactiveMechanismComponent> Mechanism;
    UPROPERTY(Replicated) FName Service;
    UPROPERTY(Replicated) bool bCarryable = false;
    UPROPERTY(Replicated) TObjectPtr<AAetherCharacter> Carrier;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Person;
    UPROPERTY(Replicated) float ReceivedPower = 0;
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit) override;
    UFUNCTION() void OnMaterialReaction(EReactiveReaction Kind,double Magnitude,FVector Vector);
    bool bWasBurning = false;
    float LastPowerTime = -100;
};
UCLASS()
class AAetherFrontierState : public AAetherAdventureState
{
    GENERATED_BODY()
public:
    UPROPERTY(Replicated) bool bSupplyRestored = false;
    UPROPERTY(Replicated) bool bBridgeReleased = false;
    UPROPERTY(Replicated) bool bPowerOn = true;
    UPROPERTY(Replicated) int32 ActivityKills = 0;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
UCLASS()
class AAetherFrontierCharacter : public AAetherCharacter
{
    GENERATED_BODY()
public:
    AAetherFrontierCharacter();
    virtual void BeginPlay() override;
    virtual void PossessedBy(AController* C) override;
    virtual void OnRep_PlayerState() override;
    virtual void SetupPlayerInputComponent(UInputComponent* I) override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool SpellUnlocked(int32 Spell) const override;
    virtual void ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPhysicsHandleComponent> CarryHandle;
    UPROPERTY(Replicated) bool bSprinting = false;
    UPROPERTY(Replicated) TObjectPtr<AAetherFrontierProp> Carried;
    UPROPERTY(Replicated) TObjectPtr<AAetherFrontierCharacter> CompanionOwner;
    UPROPERTY(Replicated) bool bHealer = false;
    UPROPERTY(Replicated) TObjectPtr<AAetherFrontierCharacter> ReviveTarget;
    TWeakObjectPtr<AAetherFrontierCharacter> RescueHolder;
    float RescueLeaseUntil=0;
    bool bFollowing=false;
    float ReviveStarted = 0;
    uint64 ReviveDamageSerial = 0;
    bool bDebugOverlay = false;
    void ToggleDebug(){bDebugOverlay=!bDebugOverlay;}
    void ToggleWeather(){ServerAction("Weather");}
    bool bPanel = false;
    int32 Panel = 0;
    float PressedAt = 0;
    float NextCompanionAction = 0;
    float NextServerAction = 0;
    float NextPotion = 0;
    int32 SelectedItem = 0;
    int32 TrackedQuest = INDEX_NONE;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> GameplayContext;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UInputAction>> InputActions;
    UFUNCTION(Exec) void AetherBind(FName Action,FKey Key);
    FKey BindingFor(FName Action) const;
    UPROPERTY() TObjectPtr<AAetherCharacter> LockedTarget;
    void ToggleLock();

    UPROPERTY(Replicated) FName CompanionId;
    UPROPERTY(Replicated) bool bCompanionHold = false;
    UPROPERTY(Replicated) FName EncounterId;
    UPROPERTY(Replicated) uint8 BossPhase = 0; // 0 armor, 1 overheat, 2 exposed
    UPROPERTY(Replicated) int32 BossVersion = 0;
    UPROPERTY(Replicated) float BossPhaseStarted = 0;
    UPROPERTY(Replicated) float BossPressure = 0;
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer) override;
    void CycleItem();
    void SellItem(){if(bPanel&&Panel==1)ServerAction("Sell",SelectedItem);}
    void BuyMana(){if(bPanel&&Panel==1)ServerAction("Buy",1);}
    void BuyRation(){if(bPanel&&Panel==1)ServerAction("Buy",2);}
    void UseMana(){if(!bPanel)ServerAction("ManaPotion");}
    void Recover(){ServerAction("Recover");}
    void PartyCommand(){if(bPanel&&Panel==5)ServerAction("PartyCommand");}
    void Invite(){if(bPanel&&Panel==5)ServerAction("Invite");}
    void AcceptInvite(){if(bPanel&&Panel==5)ServerAction("AcceptInvite");}
    void LeaveParty(){if(bPanel&&Panel==5)ServerAction("LeaveParty");}
    void Dismiss(){if(bPanel&&Panel==5)ServerAction("Dismiss");}
    AAetherPlayerState* ProfileState() const;
    void BindPersistentAbilities();
    void ApplyProfileEquipment();
    UFUNCTION(Server,Reliable) void ServerAction(FName Action,int32 Index = 0);
    UFUNCTION(Server,Reliable) void ServerWorldService(FAetherWorldServiceCommand Command);
    UFUNCTION(Client,Reliable) void WorldServiceResult(FGuid Id,EAetherServiceResult Result,int32 Revision);
    UFUNCTION(Server,Reliable) void ServerSprint(bool Enabled);
    UFUNCTION(Client,Reliable) void Notify(const FString& Message);
    void ReleaseCarry();
private:
    void PressAttack(); void ReleaseAttack();
    void SprintOn(){ServerSprint(true);} void SprintOff(){ServerSprint(false);}
    void UsePotion(){if(!bPanel)ServerAction("Potion");} void InteractV4();
    FAetherWorldServiceCommand PendingService;
    int32 MinimumServiceRevision=0;
    void Throw(){if(!bPanel)ServerAction("Throw");}
    void ClaimRewards(){if(bPanel&&Panel==2)ServerAction("Claim");}
    void Carry(){if(!bPanel)ServerAction("Carry");} void Push(){if(!bPanel)ServerAction("Push");}
    void EquipNext(){ServerAction("Equip");} void Shield(){ServerAction("Shield");}
    void SaveV4(){ServerAction("Save");} void Recruit(){ServerAction("Recruit");}
    void SplitStack(){if(bPanel&&Panel==1)ServerAction("Split");}
    void MergeStacks(){if(bPanel&&Panel==1)ServerAction("Merge");}
    void ToggleInventory(){SelectPanel(1);} void ToggleQuests(){SelectPanel(2);}
    void ToggleSkills(){SelectPanel(3);} void ToggleMap(){SelectPanel(4);}
    void ToggleParty(){SelectPanel(5);} void ToggleMenu(){SelectPanel(6);}
    void SelectPanel(int32 NewPanel){const bool Open=!bPanel||Panel!=NewPanel;Panel=NewPanel;bPanel=Open;if(Open){ServerSprint(false);ServerBlock(false);}}
    void CastSelectedV4(){if(!bPanel)TrySpell(SelectedSpell);}
    void Spell0(){SelectedSpell=0;} void Spell1(){SelectedSpell=1;} void Spell2(){SelectedSpell=2;} void Spell3(){SelectedSpell=3;}
    void Forward(float V); void Right(float V); void Yaw(float V); void Pitch(float V);
    void GuardOn(){if(!bPanel)ServerBlock(true);} void GuardOff(){ServerBlock(false);}
    void Dodge(){if(!bPanel)ServerDodge();} void JumpV4(){if(!bPanel && Alive())Jump();}
};
USTRUCT()
struct FAetherWorldLoot
{
 GENERATED_BODY()
 UPROPERTY() FGuid ClaimId;
 UPROPERTY() FVector Location=FVector::ZeroVector;
 UPROPERTY() FName Definition="Material";
 UPROPERTY() int32 Count=1;
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
    bool ValidateWorldLedger() const;
    UPROPERTY() bool bSupplyRestored = false;
    UPROPERTY() bool bBridgeReleased = false;
    UPROPERTY() bool bPowerOn = true;
    UPROPERTY() double RainKgPerM2Sec = 0;
    UPROPERTY() double AmbientTemperatureC = 20;
    UPROPERTY() FVector WindMPerSec = FVector::ZeroVector;
    UPROPERTY() TArray<FReactiveSaveRecord> World;
    UPROPERTY() FAetherEncounterRun Abbey;
    UPROPERTY() FAetherEncounterRun Relay;
};
UCLASS()
class AAetherFrontierMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AAetherFrontierMode();
    virtual void InitGame(const FString& Map,const FString& Options,FString& Error) override;
    virtual FString InitNewPlayer(APlayerController* PC,const FUniqueNetIdRepl& Id,const FString& Options,const FString& Portal) override;
    virtual void BeginPlay() override;
    virtual void RestartPlayer(AController* C) override;
    virtual void Tick(float Dt) override;
    virtual void Logout(AController* C) override;
    friend class AAetherEncounterDirector;
    UPROPERTY() TObjectPtr<AAetherEncounterDirector> Encounters;
    UPROPERTY() TObjectPtr<UAetherFrontierSave> Database;
    UPROPERTY() TArray<TObjectPtr<AAetherFrontierProp>> Props;
    UPROPERTY() TObjectPtr<AAetherFrontierCharacter> Guardian;
    UPROPERTY() TObjectPtr<AAetherFrontierCharacter> ActivityEnemy;
    UPROPERTY() TArray<TObjectPtr<AAetherFrontierCharacter>> Companions;
    TMap<TWeakObjectPtr<AAetherCharacter>,TSet<FName>> KillCredit;
    FString SavePrefix = TEXT("AetherFrontier_v4");
    bool bSmoke = false;
    bool bFailWrites = false;
    bool bFailAfterDataWrite = false;
    bool Commit(AAetherPlayerState* PS, FAetherProfile Next);
    bool CommitOffline(FAetherProfile Next);
    bool SaveWorld();
    EAetherServiceResult ExecuteWorldService(AAetherFrontierCharacter* C,const FAetherWorldServiceCommand& Command);
    void Observe(AAetherCharacter* C,FName Fact);
    FString Interact(AAetherFrontierCharacter* C);
    FString RecruitCompanion(AAetherFrontierCharacter* C,bool Healer=false);
    AAetherFrontierProp* Prop(FName Id) const;
    bool RecordCampClear(FName Definition,FGuid Instance);
    FString ClaimLoot(AAetherFrontierCharacter* C,FName Id);
    void SpawnLoot(const FAetherWorldLoot& Loot);
    bool CanChangeParty(const AAetherFrontierCharacter* C) const;
    void LeaveParty(AAetherPlayerState* PS);
    void CreditHit(AAetherCharacter* Target,AAetherCharacter* Source);
    AAetherFrontierProp* Make(FName Id,FName Service,FVector Location,FVector Scale,EAetherObjectKind Kind,const FString& Label);
private:
    void BuildWorld();
    AAetherFrontierCharacter* SpawnFighter(FVector P,EAetherFighter Type,FName Id);
    bool WriteDatabase(UAetherFrontierSave* Next);
    bool CaptureWorldCandidate(UAetherFrontierSave* Candidate) const;
    void SmokeStep();
    void CheckAnimation();
    void CheckGuidance();
    void CheckReactions();
    void CheckServices();
    float WeatherTimer = 0;
    float AreaTimer = 0;
    float Elapsed = 0; float SaveTimer = 0; float PowerTimer = 0;
    bool bLightCheckStarted = false;
    int32 SmokeStage = 0; int32 Failures = 0;
};
UCLASS()
class AAetherFrontierHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    UPROPERTY() TObjectPtr<UAetherFrontierPanel> PanelWidget;
    virtual void DrawHUD() override;
    FAetherGuidance Guidance;
    FAetherInteractionTarget Interaction;
    float NextGuidanceUpdate=0;
};
