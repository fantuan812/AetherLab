#pragma once

#include "CoreMinimal.h"

// No UObject, Actor, GAS, Chaos or Niagara dependency. One instance per world.
namespace Reactive
{
using FBodyId = uint32;
constexpr FBodyId InvalidBody = 0;
constexpr double FusionJPerKg = 334000.0;
constexpr double VaporizationJPerKg = 2256000.0;
constexpr double WaterCp = 4186.0;
constexpr double IceCp = 2100.0;

struct REACTIVECORE_API FMaterial
{
    double DryMassKg = 0.1;
    double SpecificHeatJPerKgK = 1700.0;
    double WaterCapacityKg = 0.05;
    double InitialFuelKg = 0.05;
    double IgnitionC = 300.0;
    double BurnRateKgPerSec = 0.002;
    double CombustionJPerKg = 16000000.0;
    double RetainedHeatFraction = 0.2;
    bool bLiquidConductor = false; // Only authored liquid volumes.
    double Conductivity = 0.01; // Gameplay contact weight, NOT siemens/metre.
    double ThermalCouplingWPerK = 4.0;
    double CoolingWPerK = 0.1;
    double StrengthNs = 30.0;
    double FrozenStrengthMultiplier = 0.25;
    double SealedVolumeM3 = 0.0; // Zero = open. Simplified caloric gas reservoir.
    double BurstGaugePressurePa = 150000.0;

    static FMaterial Wood();
    static FMaterial Metal();
    static FMaterial Water();
    static FMaterial Oil();
    static FMaterial Stone();
    bool IsValid() const;
    double DryCapacity() const { return FMath::Max(1.0, DryMassKg * SpecificHeatJPerKgK); }
};

struct FState
{
    uint64 RootCauseId = 0;
    FBodyId LastSource = InvalidBody; // Runtime causal attribution; cleared when loading a save.
    double EnthalpyJ = 0.0; // Shared dry body + bound water; ice at 0 C is water zero.
    double TemperatureC = 20.0; // Derived, never an independent source of truth.
    double WaterKg = 0.0;
    double ElectricalWaterKg = 0; // Authored/splash water only; rainfall never opens a liquid connection.
    double ElectricalWetness01 = 0; // Splash channel; never derived from rain water.
    double IceFraction = 0.0;
    double FuelKg = 0.0;
    double Integrity = 1.0;
    double GasEnergyJ = 0.0;
    double GaugePressurePa = 0.0;
    bool bBurning = false;
    bool bBroken = false;
    bool bBurst = false;
};

struct FEnvironment
{
    double TemperatureC = 20.0;
    double RainKgPerM2Sec = 0.0;
    FVector WindMPerSec = FVector::ZeroVector;
};

struct FStimulus
{
    uint64 InputId = 0; // Nonzero input IDs are idempotent within the replay window.
    uint64 RootCauseId = 0;
    FBodyId Source = InvalidBody;
    FBodyId Target = InvalidBody; // Zero means a normalized area distribution.
    FVector PositionCm = FVector::ZeroVector;
    double RadiusCm = 0.0;
    double HeatJ = 0.0; // Negative = extraction.
    double WaterKg = 0.0;
    double ElectricalJ = 0.0;
    bool bApplyPhysicsImpulse = true;
    FVector ImpulseNs = FVector::ZeroVector;
};

enum class EEvent : uint8 { Ignited, Extinguished, Frozen, Thawed, Steam, Shock, Broken, Impulse, Burst };
struct FEvent
{
    uint64 Sequence = 0;
    FBodyId Body = InvalidBody;
    EEvent Kind = EEvent::Ignited;
    double Magnitude = 0.0; // Steam: kg; Shock/Burst: J; others: normalized or 0.
    FVector Vector = FVector::ZeroVector; // Impulse: N.s, converted by UE adapter.
    FBodyId Source = InvalidBody;
    uint64 RootCauseId = 0;
};

struct FElectricalReceiver
{
    uint64 Id = 0; // Same physical endpoint across multiple contact shapes. Zero uses body ID.
    double LoadWeight = -1; // Negative selects mass-normalized material absorption.
    double CapacityJ = 1.e8; // Per pulse, shared by all shapes with this ID.
    double HeatFraction = 1; // Remainder is useful device energy, never another heat deposit.
    bool bTerminal = false; // Characters receive electricity but cannot bridge conductors.
};

struct FStats
{
    int32 Registered = 0;
    int32 Active = 0;
    int32 ThermalPairs = 0;
    int32 ElectricalVisits = 0;
    uint64 Steps = 0;
    uint64 RejectedInputs = 0;
    uint64 BudgetHits = 0;
    double ElectricalInputJ = 0.0;
    double ElectricalDepositedJ = 0.0;
    double ElectricalUsefulJ = 0;
    uint64 DuplicateInputs = 0;
    double ElectricalLostJ = 0.0;
    double RejectedExtractionJ = 0;
    double VentedEnergyJ = 0.0;
    double RejectedWaterKg = 0.0;
};

struct FSettings
{
    double StepSeconds = 0.05;
    double CellSizeCm = 200.0;
    double HeatReachCm = 250.0;
    int32 MaxBodies = 4096;
    int32 MaxPendingInputs = 512;
    int32 MaxThermalPairs = 16384;
    int32 MaxElectricalNodes = 128;
    int32 MaxElectricalHops = 8;
};

class REACTIVECORE_API FSimulation
{
public:
    explicit FSimulation(const FSettings& InSettings = FSettings());
    FBodyId Register(const FMaterial& Material, const FVector& PositionCm, double RadiusCm,
        double TemperatureC = 20.0, double WaterKg = 0.0);
    void Unregister(FBodyId Id);
    bool SetReceiver(FBodyId Id, const FElectricalReceiver& Receiver);
    bool Move(FBodyId Id, const FVector& PositionCm);
    bool Enqueue(const FStimulus& Input);
    bool SetEnvironment(const FEnvironment& InEnvironment);
    // Transfers only liquid water, carrying its enthalpy. No water is created or lost.
    double TransferLiquid(FBodyId From, FBodyId To, double MaxKg);
    double WithdrawLiquid(FBodyId From, double MaxKg);
    // Validates the entire batch before mutation; transient inputs/events are discarded on success.
    bool RestoreStates(const TMap<FBodyId, FState>& States);
    void Step();
    const FState* Find(FBodyId Id) const;
    const FMaterial* FindMaterial(FBodyId Id) const;
    const FStats& GetStats() const { return Stats; }
    const FSettings& GetSettings() const { return Settings; }
    bool HasPendingInputs() const { return !Pending.IsEmpty(); }
    const FEnvironment& GetEnvironment() const { return Environment; }
    TArray<FEvent> DrainEvents();
    const TArray<FBodyId>& GetChangedBodies() const { return Changed; }
    TArray<FBodyId> Query(const FVector& PositionCm, double RadiusCm) const;
    // Optional contact/occlusion gate, called synchronously during Step on the owning thread.
    TFunction<bool(FBodyId, FBodyId)> CanExchange;
    TFunction<bool(FBodyId, FBodyId)> CanConduct;
    TFunction<bool(FBodyId, FBodyId)> CanReceiveInput; // Target, original source.
    TFunction<double(FBodyId, FBodyId)> ContactCostMeters;
    static double InitialEnthalpy(const FMaterial& M, double TemperatureC, double WaterKg);

private:
    struct FBody
    {
        FElectricalReceiver Receiver;
        FMaterial Material;
        FState State;
        FVector Position = FVector::ZeroVector;
        double RadiusCm = 50.0;
        double QuietSeconds = 0.0;
        FIntVector Cell = FIntVector::ZeroValue;
    };
    FSettings Settings;
    FEnvironment Environment;
    FStats Stats;
    TMap<FBodyId, FBody> Bodies;
    TMap<FIntVector, TArray<FBodyId>> Grid;
    TSet<FBodyId> Active;
    TArray<FBodyId> Changed;
    TArray<FStimulus> Pending;
    TArray<FEvent> Events;
    FBodyId NextId = 1;
    uint64 NextEvent = 1;
    uint64 NextCause = 1;
    struct FInputKey
    {
        FBodyId Source;uint64 Sequence;
        bool operator==(const FInputKey& Other) const {return Source==Other.Source&&Sequence==Other.Sequence;}
        friend uint32 GetTypeHash(const FInputKey& Key){return HashCombine(::GetTypeHash(Key.Source),::GetTypeHash(Key.Sequence));}
    };
    TSet<FInputKey> RecentInputs;
    TArray<FInputKey> InputOrder;
    double MaxRadiusCm = 0.0;

    FIntVector CellFor(const FVector& Position) const;
    void Wake(FBodyId Id);
    void Resolve(FBodyId Id, FBody& Body);
    void Apply(FBodyId Id, const FStimulus& Input, double Weight);
    void Conduct(const TArray<FBodyId>& Origins, double EnergyJ, FBodyId Source, uint64 Cause);
    void ExchangeHeat();
    void React(FBodyId Id, FBody& Body);
    void Emit(FBodyId Id, EEvent Kind, double Magnitude = 0.0, const FVector& Vector = FVector::ZeroVector);
    double Conductivity(const FBody& Body) const;
};
}
