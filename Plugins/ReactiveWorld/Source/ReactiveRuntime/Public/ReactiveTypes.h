#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ReactiveSimulation.h"
#include "ReactiveTypes.generated.h"

UENUM(BlueprintType)
enum class EReactiveMaterialPreset : uint8 { Wood, Metal, Water, Oil, Stone };

UENUM(BlueprintType)
enum class EReactiveReaction : uint8 { Ignited, Extinguished, Frozen, Thawed, Steam, Shock, Broken, Impulse, Burst };

USTRUCT(BlueprintType)
struct REACTIVERUNTIME_API FReactiveMaterialParameters
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Material", meta=(ClampMin="0.001")) double DryMassKg = 0.1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thermal", meta=(ClampMin="1")) double SpecificHeatJPerKgK = 1700;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water", meta=(ClampMin="0")) double WaterCapacityKg = 0.05;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combustion", meta=(ClampMin="0")) double InitialFuelKg = 0.05;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combustion", meta=(ClampMin="101")) double IgnitionC = 300;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combustion", meta=(ClampMin="0")) double BurnRateKgPerSec = 0.002;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combustion", meta=(ClampMin="0")) double CombustionJPerKg = 16000000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combustion", meta=(ClampMin="0", ClampMax="1")) double RetainedHeatFraction = 0.2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Electrical", meta=(ClampMin="0", ClampMax="1")) double Conductivity = 0.01;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thermal", meta=(ClampMin="0")) double ThermalCouplingWPerK = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thermal", meta=(ClampMin="0")) double CoolingWPerK = 0.1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Structure", meta=(ClampMin="0.01")) double StrengthNs = 30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Structure", meta=(ClampMin="0.01", ClampMax="1")) double FrozenStrengthMultiplier = 0.25;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pressure", meta=(ClampMin="0")) double SealedVolumeM3 = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pressure", meta=(ClampMin="1")) double BurstGaugePressurePa = 150000;
    Reactive::FMaterial ToCore() const;
};

UCLASS(BlueprintType)
class REACTIVERUNTIME_API UReactiveMaterialAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive") FReactiveMaterialParameters Parameters;
};

USTRUCT(BlueprintType)
struct REACTIVERUNTIME_API FReactiveStimulus
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite, Category="Reactive") TObjectPtr<AActor> SourceActor;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive") FVector PositionCm = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive", meta=(ClampMin="0", ClampMax="2000")) double RadiusCm = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive") double HeatJ = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive", meta=(ClampMin="0")) double WaterKg = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive", meta=(ClampMin="0")) double ElectricalJ = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive") FVector ImpulseNs = FVector::ZeroVector;
};

USTRUCT()
struct REACTIVERUNTIME_API FReactiveSaveRecord
{
    GENERATED_BODY()
    UPROPERTY() FName StableId;
    UPROPERTY() FTransform Transform;
    UPROPERTY() uint32 MaterialSignature = 0;
    UPROPERTY() double EnthalpyJ = 0;
    UPROPERTY() double WaterKg = 0;
    UPROPERTY() double FuelKg = 0;
    UPROPERTY() double Integrity = 1;
    UPROPERTY() double GasEnergyJ = 0;
    UPROPERTY() bool bBurning = false;
    UPROPERTY() bool bBroken = false;
    UPROPERTY() bool bBurst = false;
};

USTRUCT(BlueprintType)
struct REACTIVERUNTIME_API FReactiveSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Reactive") double TemperatureC = 20;
    UPROPERTY(BlueprintReadOnly, Category="Reactive") double WaterKg = 0;
    UPROPERTY(BlueprintReadOnly, Category="Reactive") double IceFraction = 0;
    UPROPERTY(BlueprintReadOnly, Category="Reactive") double FuelKg = 0;
    UPROPERTY(BlueprintReadOnly, Category="Reactive") double Integrity = 1;
    UPROPERTY(BlueprintReadOnly, Category="Reactive") double GaugePressurePa = 0;
    UPROPERTY(BlueprintReadOnly, Category="Reactive") bool bBurning = false;
    UPROPERTY(BlueprintReadOnly, Category="Reactive") bool bBroken = false;
    UPROPERTY(BlueprintReadOnly, Category="Reactive") bool bBurst = false;
    static FReactiveSnapshot FromCore(const Reactive::FState& S);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FReactiveReactionEvent, EReactiveReaction, Reaction, double, Magnitude, FVector, Vector);
