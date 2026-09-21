#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Persistence/AetherFrontierSave.h"
#include "World/AetherFrontierState.h"
#include "AetherWorldState.h"
#include "AetherPersistence.h"
#include "Persistence/AetherWorldBootstrap.h"
#include "Persistence/AetherWorldCheckpoint.h"
#include "Commands/AetherProfileCommand.h"
#include "AetherFrontierMode.generated.h"

class AAetherPlayerController;
class AAetherNativeContainer;
class AAetherFrontierProp;
class AAetherPlayerState;
enum class EAetherFighter : uint8;
class AAetherFrontierCharacter;
class UAetherPhysicsDamageComponent;
class UAetherTraversalComponent;
class UPhysicsHandleComponent;
class UInputMappingContext;
class UInputAction;
class UAetherFrontierPanel;
struct FAetherWorldPlacement;
struct FAetherInteractionTarget;

// 权威装配与事务协调入口；不允许 UI 直接发布候选数据库。
UCLASS()
class AETHERLAB_API AAetherFrontierMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AAetherFrontierMode();
    virtual void InitGame(const FString& Map,const FString& Options,FString& Error) override;
    virtual FString InitNewPlayer(APlayerController* PC,const FUniqueNetIdRepl& Id,const FString& Options,const FString& Portal) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
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
    bool IsNativeMode() const{return bNativeMode;}
    bool NativeSceneReady() const{return bNativeSceneReady&&bNativeBaselineReady;}
    bool IsTravelRegionReady(FVector Destination) const;
    void ReleaseNativePawn(AAetherFrontierCharacter* Pawn);
    bool RecoverNativePlayer(AAetherFrontierCharacter* Pawn);
    FString ExecuteNativeSceneService(AAetherPlayerController& Controller,const FAetherPlayerCommand& Command);
    bool AuthorizeNativeContainer(AAetherPlayerController& Controller,const FString& Id,bool bOpen);
    const FAetherWorldStateV10* NativeWorldView() const{return NativeWorld.IsSet()?&NativeWorld.GetValue():nullptr;}
    bool bFailWrites = false;
    TSharedPtr<IAetherSnapshotStore> Storage;
    TMap<FString,int32> ClosureAcks;
    bool bClosureFailed=false;
    bool bFailAfterDataWrite = false;
    EAetherInventoryResult ExecuteInventory(AAetherFrontierCharacter* C,const FAetherInventoryCommand& Command,int32& Revision,int32& Moved,FGuid TradeAuthorization=FGuid());
    bool Commit(AAetherPlayerState* PS, FAetherProfile Next,FName WorldFact=NAME_None,FName FactSource=NAME_None);
    bool CommitOffline(FAetherProfile Next);
    bool SaveWorld();
    EAetherServiceResult ExecuteWorldService(AAetherFrontierCharacter* C,const FAetherWorldServiceCommand& Command,const FAetherInteractionTarget* Selection=nullptr);
    void Observe(AAetherCharacter* C,FName Fact,FName Source=NAME_None);
    // 仅服务器内部脚本可即时选目标；网络入口必须使用 InteractTarget。
    FString Interact(AAetherFrontierCharacter* C);
    FString InteractTarget(AAetherFrontierCharacter* C,const FAetherInteractionTarget& Target);
    FString RecruitCompanion(AAetherFrontierCharacter* C,bool Healer=false);
    AAetherFrontierProp* Prop(FName Id) const;
    bool RecordCampClear(FName Definition,FGuid Instance);
    FString ClaimLoot(AAetherFrontierCharacter* C,FName Id);
    void SpawnLoot(const FAetherWorldLoot& Loot);
    bool ExecutePartyAction(AAetherFrontierCharacter* C,FName Action,AAetherPlayerState* InviteTarget=nullptr,AAetherFrontierCharacter* Companion=nullptr);
    int32 PartySize(const FString& Leader) const;
    bool CanChangeParty(const AAetherFrontierCharacter* C) const;
    void LeaveParty(AAetherPlayerState* PS);
    void CreditHit(AAetherCharacter* Target,AAetherCharacter* Source);
    AAetherFrontierProp* Make(FName Id,FName Service,FVector Location,FVector Scale,EAetherObjectKind Kind,const FString& Label);
private:
    // 只有服务器启动装配持有这些值；旧 Database 是导航/流送的只读投影，不能提交它。
    bool bNativeMode=true,bNativeSceneReady=false,bNativeBaselineReady=false,bNativeFailureReported=false;
    TFuture<FAetherWorldCheckpointResult> NativeBaseline;
    double NativeBaselineStarted=0;
    TOptional<FAetherWorldStateV10> NativeWorld;
    TMap<TWeakObjectPtr<AAetherPlayerController>,TFuture<FAetherStoreReadResult>> NativeLogins;
    TSet<TWeakObjectPtr<AAetherPlayerController>> NativePlayersReady,NativeLoginRejected;
    TMap<TWeakObjectPtr<AAetherPlayerController>,TWeakObjectPtr<AAetherNativeContainer>> NativeContainerSessions;
    UPROPERTY() TMap<FString,TObjectPtr<AAetherNativeContainer>> NativeContainers;
    void TickNativeStartup();
    void BeginNativeLogin(AAetherPlayerController* PC);
    bool RestoreNativeScene(const FAetherWorldStateV10& World,const TMap<FString,int64>& Profiles,const TArray<FAetherContainerRestoreDescriptor>& Containers,FString& Reason);
    void PublishNativeWorld(const FAetherWorldStateV10& World);
    void PublishNativeContainer(const FAetherContainerStateV10& Container);
    void TickNativeContainers();
    struct FContainerCreation {FAetherContainerStateV10 Expected;TFuture<FAetherStoreReadResult> Future;};
    TMap<FString,FContainerCreation> NativeContainerCreates;
    TMap<FString,double> NativeContainerRetry;
    bool PublishNativeState(AAetherPlayerController& PC,const FAetherProfileStateV10& Profile,const FAetherWorldStateV10* World,const FAetherContainerStateV10* Container);
    bool ResolveNativeContext(AAetherPlayerController& PC,const FAetherPlayerCommand& Command,const FAetherProfileStateV10& Profile,FAetherProfileCommandContext& Context);
    void FailNativeScene(const FString& Reason);
    FAetherEntityRegistry Registry;
    int32 RegionLoads=0,RegionUnloads=0;
    void UpdateRegions(const TArray<FVector>& Players);
    struct FFrozenRegionActor
    {
        TWeakObjectPtr<AAetherFrontierProp> Actor;
        bool Enabled=false,Ticking=false,Simulating=false;
        FVector Linear=FVector::ZeroVector,Angular=FVector::ZeroVector;
        TArray<TWeakObjectPtr<UActorComponent>> TickingComponents;
    };
    bool bNativeRegionBarrier=false;
    TArray<FFrozenRegionActor> FrozenRegionActors;
    TArray<FName> FrozenRegionBodies;
    TFuture<FAetherWorldCheckpointResult> NativeRegionSave;
    TOptional<FAetherWorldCheckpointResult> NativeRegionOutcome;
    void AdvanceNativeRegions(const TArray<FName>& Unload,const TArray<const FAetherWorldPlacement*>& Load);
    struct FNativeCampWrite {FGuid Instance;TFuture<FAetherWorldCheckpointResult> Write;};
    TMap<FName,FNativeCampWrite> NativeCampWrites;
    bool RecordNativeCampClear(FName Definition,FGuid Instance);
    void SettleNativeEncounter(FAetherEncounterRun& Run);
    FString ClaimNativeLegacyLoot(AAetherFrontierCharacter* Character,FName StableId);
    bool CaptureNativeWorld(const FAetherWorldStateV10& Previous,FAetherWorldStateV10& Candidate,FString& Reason);
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
