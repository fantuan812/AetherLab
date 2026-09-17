#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ReactiveTypes.h"
#include "ReactiveWorldSubsystem.generated.h"

class UReactiveBodyComponent;

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
    double TransferWater(UReactiveBodyComponent* From, UReactiveBodyComponent* To, double MaxKg);
    double WithdrawWater(UReactiveBodyComponent* From, double MaxKg);
    bool Capture(TArray<FReactiveSaveRecord>& Records) const;
    bool Restore(const TArray<FReactiveSaveRecord>& Records);
    double LastStepMilliseconds = 0;
    int32 OcclusionTraces = 0;
    int32 OcclusionCacheHits = 0;
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
    bool IsAuthority() const;
    bool CanBodiesExchange(Reactive::FBodyId A, Reactive::FBodyId B) const;
    TUniquePtr<Reactive::FSimulation> Simulation;
    TMap<Reactive::FBodyId, TWeakObjectPtr<UReactiveBodyComponent>> Components;
    TSet<Reactive::FBodyId> Moving;
    TMap<FName,uint64> ReceiverGroups;
    uint64 NextReceiverGroup=uint64(1)<<32;
    double Accumulator = 0;
    double DroppedSeconds = 0;
    mutable TMap<uint64, bool> ContactCache;
};
