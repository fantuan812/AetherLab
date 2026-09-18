#pragma once
#include "CoreMinimal.h"
#include "AetherAdventure.h"
#include "AetherProgression.h"
#include "ReactiveMechanismComponent.h"
#include "AetherEncounters.h"
#include "AetherGuide.h"
#include "AetherServices.h"
#include "AetherQuestRuntime.h"
#include "AetherWorldState.h"
#include "AetherWorldCapability.h"
#include "AetherPersistence.h"
struct FAetherWorldPlacement;
#include "AetherFrontier.generated.h"
class UAetherPhysicsDamageComponent;
class UAetherTraversalComponent;
class UPhysicsHandleComponent;
class UInputMappingContext;
class UInputAction;
class UAetherFrontierPanel;
class USkeletalMeshComponent;

UCLASS()
class AAetherFrontierProp : public AAetherWorldObject, public IAetherWorldCapability
{
    GENERATED_BODY()
public:
    AAetherFrontierProp();
    virtual bool HasWorldCapability(FName Capability) const override {return Capability=="LiquidReceiver"?bAcceptsWater:Capabilities.Contains(Capability);}
    virtual UReactiveBodyComponent* ReactionBody() const override {return Reactive;}
    UPROPERTY(VisibleAnywhere) TObjectPtr<UReactiveMechanismComponent> Mechanism;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAetherPhysicsDamageComponent> PhysicsDamage;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAetherTraversalComponent> Traversal;
    UPROPERTY(Replicated) FName Service;
    UPROPERTY(Replicated) TArray<FName> Capabilities;
    UPROPERTY(Replicated) bool bCarryable = false;
    UPROPERTY(Replicated) TObjectPtr<AAetherCharacter> Carrier;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Person;
    UPROPERTY(Replicated) float ReceivedPower = 0;
    UPROPERTY(Replicated) bool bAcceptsWater = false;
    UPROPERTY(Replicated) bool bInspectableFire = false;
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit) override;
    UFUNCTION() void OnMaterialReaction(EReactiveReaction Kind,double Magnitude,FVector Vector);
    UFUNCTION() void OnElectricalWindow(const FReactiveElectricalWindow& Window);
    UPROPERTY(Replicated) bool bGlobalPowerService = false;
    UPROPERTY(Replicated) bool bWorkshopService = false;
    UPROPERTY(Replicated) bool bExtinguished = false;
    uint8 LastFeedback = 255;
    void UpdateReactionFeedback();
    bool bWasBurning = false;
    float LastPowerTime = -100;
};
UCLASS()
class AAetherFrontierState : public AAetherAdventureState
{
    GENERATED_BODY()
public:
    UPROPERTY(Replicated) bool bSupplyRestored = false;
    UPROPERTY(Replicated) bool bWorkshopRestored = false;
    UPROPERTY(Replicated) bool bBridgeReleased = false;
    UPROPERTY(Replicated) bool bPowerOn = true;
    UPROPERTY(Replicated) int32 ActivityKills = 0;
    UPROPERTY(Replicated) int32 ClosurePhase = 0;
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
    bool bTravelPending=false;
    FVector TravelDestination,TravelOrigin;
    float TravelStarted=0;
    void BeginSafeTravel(FVector Destination);
    void UpdateSafeTravel();
    int32 SelectedItem = 0;
    FGuid SelectedInstance,MergeDestination;
    int32 InventoryQuantity=1,MinimumInventoryRevision=0;
    FAetherInventoryCommand PendingInventory;
    void SubmitInventory(FName Action,FName Definition=NAME_None);
    UFUNCTION(Server,Reliable) void ServerInventory(FAetherInventoryCommand Command);
    UFUNCTION(Client,Reliable) void InventoryResult(FGuid Id,EAetherInventoryResult Result,int32 Revision,int32 Transferred);
    FName TrackedQuest;
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
    void SellItem(){if(bPanel&&Panel==1)SubmitInventory("Sell");}
    void BuyMana(){if(bPanel&&Panel==1)SubmitInventory("Buy","ManaPotion");}
    void BuyRation(){if(bPanel&&Panel==1)SubmitInventory("Buy","Ration");}
    void UseMana(){if(!bPanel)SubmitInventory("Use","ManaPotion");}
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
    void CheckClosureClient(float Dt);
    UFUNCTION(Client,Reliable) void ClientClosureAction(FName Action,FRotator Look);
    UFUNCTION(Server,Reliable) void ServerClosureAck(int32 Phase,bool Passed);
    float ClosureClientTime=0;
    int32 ClosureSeenPhase=0;
private:
    void PressAttack(); void ReleaseAttack();
    void SprintOn(){ServerSprint(true);} void SprintOff(){ServerSprint(false);}
    void UsePotion(){if(!bPanel)SubmitInventory("Use","Potion");} void InteractV4();
    FAetherWorldServiceCommand PendingService;
    int32 MinimumServiceRevision=0;
    void Throw(){if(!bPanel)ServerAction("Throw");}
    void ClaimRewards(){if(bPanel&&Panel==2)ServerAction("Claim");}
    void Carry(){if(!bPanel)ServerAction("Carry");} void Push(){if(!bPanel)ServerAction("Push");}
    void EquipNext(){SubmitInventory("CycleMain");} void Shield(){SubmitInventory("CycleOff");}
    void SaveV4(){ServerAction("Save");} void Recruit(){ServerAction("Recruit");}
    void SplitStack(){if(bPanel&&Panel==1)SubmitInventory("Split");}
    void MergeStacks(){if(bPanel&&Panel==1)SubmitInventory("Merge");}
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
    bool ValidateWorldLedger() const;
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
    bool bWorldRestoreFailed=false;
    bool bFailWrites = false;
    TSharedPtr<IAetherSnapshotStore> Storage;
    TMap<FString,int32> ClosureAcks;
    bool bClosureFailed=false;
    bool bFailAfterDataWrite = false;
    EAetherInventoryResult ExecuteInventory(AAetherFrontierCharacter* C,const FAetherInventoryCommand& Command,int32& Revision,int32& Moved);
    bool Commit(AAetherPlayerState* PS, FAetherProfile Next,FName WorldFact=NAME_None,FName FactSource=NAME_None);
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
    bool ExecutePartyAction(AAetherFrontierCharacter* C,FName Action);
    bool CanChangeParty(const AAetherFrontierCharacter* C) const;
    void LeaveParty(AAetherPlayerState* PS);
    void CreditHit(AAetherCharacter* Target,AAetherCharacter* Source);
    AAetherFrontierProp* Make(FName Id,FName Service,FVector Location,FVector Scale,EAetherObjectKind Kind,const FString& Label);
private:
    FAetherEntityRegistry Registry;
    int32 RegionLoads=0,RegionUnloads=0;
    void UpdateRegions(const TArray<FVector>& Players);
    void ApplyObjectDefinition(AAetherFrontierProp* A,FName Definition);
    AAetherFrontierProp* SpawnPlacement(const FAetherWorldPlacement& Placement);
    void RebuildWorldLinks();
    void BuildWorld();
    void BuildWorkshop();
    void CaptureWorkshop();
    AAetherFrontierCharacter* SpawnFighter(FVector P,EAetherFighter Type,FName Id);
    bool WriteDatabase(UAetherFrontierSave* Next);
    void CollectPublicFacts(FAetherWorldFacts& Facts) const;
    void RefreshWorldProgress();
    bool CaptureWorldCandidate(UAetherFrontierSave* Candidate) const;
    void SmokeStep();
    void CheckAnimation();
    void CheckGuidance();
    void CheckReactions();
    void CheckServices();
    void CheckDataContracts();
    void CheckV9();
    void CheckClosure();
    int32 ClosureStage=0;
    float ClosureAt=0;
    int32 ClosureMaterialTotal=0;
    TWeakObjectPtr<AAetherFrontierCharacter> ClosureBuddy;
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
