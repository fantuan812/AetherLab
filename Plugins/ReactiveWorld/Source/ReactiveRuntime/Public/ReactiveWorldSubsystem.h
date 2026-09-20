#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ReactiveTypes.h"
#include "ReactiveWorldSubsystem.generated.h"

class UReactiveBodyComponent;
class UPrimitiveComponent;

UCLASS()
class REACTIVERUNTIME_API UReactiveWorldSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    UFUNCTION(BlueprintCallable, Category="Reactive") bool Submit(UReactiveBodyComponent* Target, const FReactiveStimulus& Input);
    UFUNCTION(BlueprintCallable, Category="Reactive") bool SetWeather(double AmbientTemperatureC, double RainKgPerM2Sec, FVector WindMPerSec);
    UFUNCTION(BlueprintPure, Category="Reactive") FString GetStatsText() const;
    Reactive::FBodyId RegisterBody(UReactiveBodyComponent* Body);
    void UnregisterBody(Reactive::FBodyId Id);
    const Reactive::FSimulation* GetSimulation() const { return Simulation.Get(); }
    void TrackMovement(Reactive::FBodyId Id) { Moving.Add(Id); }
    AActor* GetBodyOwner(Reactive::FBodyId Id) const;
    double TransferWater(UReactiveBodyComponent* From, UReactiveBodyComponent* To, double MaxKg, AActor* SourceActor = nullptr);
    double WithdrawWater(UReactiveBodyComponent* From, double MaxKg);
    bool Capture(TArray<FReactiveSaveRecord>& Records) const;
    bool Restore(const TArray<FReactiveSaveRecord>& Records,bool bPartial=false);
    // 游戏线程上的区域屏障：脱离求解与跨区交换，但 Capture 继续返回冻结记录。
    bool FreezeForPersistence(const TArray<FName>& Ids);
    bool ResumeFrozen(const TArray<FName>& Ids);
    void DiscardFrozen(const TArray<FName>& Ids);
    bool IsFrozen(FName Id) const{return FrozenRecords.Contains(Id);}
    // Greybox tolerances: enter at 2 cm for two fixed steps, leave beyond 6 cm immediately.
    double ContactEnterCm=2,ContactExitCm=6;
    uint32 ContactConfirmSteps=2;
    uint64 ContactBudgetHits=0;
    TArray<FReactiveContact> GetContacts() const;
    double LastStepMilliseconds = 0;
    double LastContactMilliseconds = 0;
    int32 OcclusionTraces = 0;
    int32 OcclusionCacheHits = 0;
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
    TMap<FName,FReactiveSaveRecord> FrozenRecords;
    TMap<FName,TWeakObjectPtr<UReactiveBodyComponent>> FrozenBodies;
    bool IsAuthority() const;
    bool CanBodiesExchange(Reactive::FBodyId A, Reactive::FBodyId B) const;
    bool CanBodiesTransferLiquid(Reactive::FBodyId A,Reactive::FBodyId B) const;
    void UpdateElectricalContacts(uint64 StepId);
    void AdvanceLiquidPorts(double Step,uint64 StepId);
    void PublishElectricalWindows();
    FName ContactBodyName(Reactive::FBodyId Id) const;
    struct FContactState
    {
        FReactiveContact Record;
        TWeakObjectPtr<UPrimitiveComponent> PrimitiveA,PrimitiveB;
        uint64 LastObservedStep=0;
        uint32 Confirmations=0;
    };
    TMap<uint64,FContactState> ElectricalContacts;
    TMap<Reactive::FBodyId,TArray<Reactive::FBodyId>> ElectricalAdjacency;
    mutable TMap<uint64,FReactiveContact> ThermalContacts;
    TMap<uint64,FReactiveContact> LiquidContacts;
    uint64 NextContactVersion=1;
    double LastBudgetWarningAt=-100;
    TMap<uint64,TWeakObjectPtr<UReactiveBodyComponent>> ElectricalRecipients;
    TUniquePtr<Reactive::FSimulation> Simulation;
    TMap<Reactive::FBodyId, TWeakObjectPtr<UReactiveBodyComponent>> Components;
    TSet<Reactive::FBodyId> Moving;
    TMap<FName,uint64> ReceiverGroups;
    uint64 NextReceiverGroup=uint64(1)<<32;
    double Accumulator = 0;
    double DroppedSeconds = 0;
    mutable TMap<uint64, bool> ContactCache;
};
