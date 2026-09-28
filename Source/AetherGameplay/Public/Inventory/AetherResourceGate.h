#pragma once
#include "Components/ActorComponent.h"
#include "Inventory/AetherConsumableEffect.h"
#include "Templates/Function.h"
#include "Effects/AetherEffectEvent.h"
#include "AetherResourceGate.generated.h"

class AAetherCharacter;
struct FAetherResourceGateMetrics
{
    uint64 MergedIntervals=0;
    int32 HighWater=0;
    int32 DeferredCount=0;
    uint64 DeferredTotal=0,RecoveryCount=0,PublicationFailures=0;
    double OldestWaitSeconds=0,PersistenceWaitSeconds=0;
};
// 每个 Pawn 的资源生命屏障。旧 Pawn 被销毁时队列随生命结束，不转投同角色的新 Pawn。
UCLASS()
class AETHERGAMEPLAY_API UAetherResourceGate : public UActorComponent
{
    GENERATED_BODY()
public:
    UAetherResourceGate();
    // 调用方已排空旧写者，并已恢复新 Pawn 的装备上限；开始完整资源重生，恢复期间禁止输入。
    void BlockForInitialLoad(){if(!Receiver)bRecovering=true;}
    bool BeginFullRespawn(const FString& ServerCharacterId);
    bool FinishRecovery();
    bool Reserve(FGuid Command,FAetherResourceStateV10& Before);
    bool CancelKnownUncommitted(FGuid Command);
    bool Publish(const FAetherConsumableReceiver& Expected);
    bool IsBlocked() const{return WaitingForPersistence()||(!bDraining&&!Deferred.IsEmpty());}
    bool IsEnabled() const{return Receiver.IsValid();}
    bool IsRecovering() const{return bRecovering;}
    bool HasUseSummary() const{return bUseSummaryReady;}
    bool IsStorageSlow() const{return bSlowStorage;}
    FAetherResourceGateMetrics Inspect() const;
    float UseReadyTime() const{return UseReadyAtServerTime;}
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    FGuid Reservation() const{return Reserved;}
    FAetherConsumableReceiver* GetReceiver(){return Receiver.Get();}
    // true 表示整个动作已排队或当前生命已终止；调用者必须立即返回，不能继续死亡/奖励等副作用。
    bool Defer(TUniqueFunction<void()> WholeAction,EAetherEffectEventKind Kind=EAetherEffectEventKind::LegacyAction);
    bool DeferInterval(const FAetherResourceAdvanceInterval& Interval,TUniqueFunction<void(double)> Apply);
    bool IsFaulted() const{return bFaulted;}
    bool IsPresentationSettled() const{return Receiver.IsValid()&&!IsBlocked()&&!bDraining&&Deferred.IsEmpty();}
    bool IsEffectProjection() const{return bEffectProjection;}
    bool Synchronize();
    void Fault(const FString& Reason);
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* ThisTick) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Replicated) bool bUseSummaryReady=false;
    UPROPERTY(Replicated) bool bSlowStorage=false;
    UPROPERTY(Replicated) float UseReadyAtServerTime=0;
    int64 PublishedUseDeadline=-1;
    bool WaitingForPersistence() const{return bRecovering||Reserved.IsValid()||bPublishing||bFaulted;}
    FAetherResourceStateV10 Sample() const;
    TUniquePtr<FAetherConsumableReceiver> Receiver;
    struct FDeferredAction
    {
        TUniqueFunction<void()> Action;
        double QueuedAt=0;
        TOptional<FAetherResourceAdvanceInterval> Interval;
        TUniqueFunction<void(double)> ApplyInterval;
        FAetherEffectEventIdentity Identity;
        TArray<double> Steps;
        int32 NextStep=0;
    };
    TArray<FDeferredAction> Deferred;
    uint64 NextSequence=0,MergedIntervals=0;
    int32 HighWater=0;
    int32 PendingSteps=0;
    uint64 DeferredTotal=0,RecoveryCount=0,PublicationFailures=0;
    double PersistenceWaitStarted=0;
    FGuid Reserved;
    TOptional<FAetherResourceStateV10> ReservedBefore;
    bool bRecovering=false,bPublishing=false,bFaulted=false,bDraining=false;
    bool bEffectProjection=false;
    bool bReservedResourcesPublished=false;
    TSet<FGuid> PublishedDeliveries;
};
