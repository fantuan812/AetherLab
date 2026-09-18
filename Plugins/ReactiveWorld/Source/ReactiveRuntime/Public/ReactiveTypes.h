#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ReactiveSimulation.h"
#include "ReactiveTypes.generated.h"

UENUM(BlueprintType)
enum class EReactiveMaterialPreset : uint8 { Wood, Metal, Water, Oil, Stone };

UENUM(BlueprintType)
enum class EReactiveIceSupport : uint8 { Liquid, FreezePending, Bearing, Thawing };

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Electrical") bool bLiquidConductor = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thermal", meta=(ClampMin="0")) double ThermalCouplingWPerK = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thermal", meta=(ClampMin="0")) double CoolingWPerK = 0.1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Structure", meta=(ClampMin="0.01")) double StrengthNs = 30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Structure", meta=(ClampMin="0")) double CutResistanceJ = 0;
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
    UPROPERTY() uint64 InputId = 0;
    UPROPERTY() uint64 RootCauseId = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive") FVector PositionCm = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive", meta=(ClampMin="0", ClampMax="2000")) double RadiusCm = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive") double HeatJ = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive", meta=(ClampMin="0")) double WaterKg = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive", meta=(ClampMin="0")) double ElectricalJ = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive", meta=(ClampMin="0")) double CuttingWorkJ = 0;
    UPROPERTY() bool bApplyPhysicsImpulse = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive") FVector ImpulseNs = FVector::ZeroVector;
};

UENUM(BlueprintType)
enum class EReactiveContactChannel : uint8 { Thermal, Electrical, Liquid };
USTRUCT(BlueprintType)
struct REACTIVERUNTIME_API FReactiveContact
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName A;
    UPROPERTY(BlueprintReadOnly) FName B;
    UPROPERTY() uint32 BodyA=0;
    UPROPERTY() uint32 BodyB=0;
    UPROPERTY(BlueprintReadOnly) EReactiveContactChannel Channel=EReactiveContactChannel::Electrical;
    UPROPERTY(BlueprintReadOnly) FVector PositionCm=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) bool bValid=false;
    UPROPERTY() uint64 Version=0;
    UPROPERTY() uint64 LastConfirmedStep=0;
};
USTRUCT(BlueprintType)
struct REACTIVERUNTIME_API FReactiveLiquidPort
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName PortId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName TargetStableId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector LocalPositionCm=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector TargetLocalPositionCm=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) double MaxGapCm=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) double MaxKgPerSecond=.1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=true;
};
USTRUCT(BlueprintType)
struct REACTIVERUNTIME_API FReactiveElectricalExposure
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Source=nullptr;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Receiver=nullptr;
    UPROPERTY(BlueprintReadOnly) FName SourceStableId;
    UPROPERTY(BlueprintReadOnly) FName ReceiverStableId;
    UPROPERTY() uint64 ReactionId=0;
    UPROPERTY() uint64 RootCauseId=0;
    UPROPERTY() uint64 StepId=0;
    UPROPERTY() uint64 ReceiverId=0;
    UPROPERTY(BlueprintReadOnly) double DeliveredJ=0;
    UPROPERTY(BlueprintReadOnly) double HeatJ=0;
    UPROPERTY(BlueprintReadOnly) double UsefulJ=0;
    UPROPERTY(BlueprintReadOnly) double DurationSeconds=0;
    UPROPERTY(BlueprintReadOnly) FReactiveContact Contact;
};
USTRUCT(BlueprintType)
struct REACTIVERUNTIME_API FReactiveElectricalWindow
{
    GENERATED_BODY()
    UPROPERTY() uint64 StepId=0;
    UPROPERTY() uint64 ReceiverId=0;
    UPROPERTY(BlueprintReadOnly) double DeliveredJ=0;
    UPROPERTY(BlueprintReadOnly) double HeatJ=0;
    UPROPERTY(BlueprintReadOnly) double UsefulJ=0;
    UPROPERTY(BlueprintReadOnly) double DurationSeconds=0;
    UPROPERTY(BlueprintReadOnly) TArray<FReactiveElectricalExposure> Contributions;
    double UsefulPowerW() const {return DurationSeconds>=.001&&FMath::IsFinite(DurationSeconds)?UsefulJ/DurationSeconds:0;}
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReactiveElectricalWindowEvent,const FReactiveElectricalWindow&,Window);

USTRUCT()
struct REACTIVERUNTIME_API FReactiveSaveRecord
{
    GENERATED_BODY()
    UPROPERTY() FName StableId;
    UPROPERTY() FTransform Transform;
    UPROPERTY() bool bGateOpen = false;
    UPROPERTY() bool bHasMechanism = false;
    UPROPERTY() bool bSupportReleased = false;
    UPROPERTY() bool bSourceEnabled = true;
    UPROPERTY() double RemainingEnergyJ = 0;
    UPROPERTY() double SourceAge = 0;
    UPROPERTY() uint32 MaterialSignature = 0;
    UPROPERTY() int32 MaterialSchema = 0; // Legacy saves omit this field; new captures write 1.
    UPROPERTY() double EnthalpyJ = 0;
    UPROPERTY() double WaterKg = 0;
    UPROPERTY() double ElectricalWaterKg = 0;
    UPROPERTY() double ElectricalWetness01 = 0;
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
    UPROPERTY(BlueprintReadOnly, Category="Reactive") double ElectricalWaterKg = 0;
    UPROPERTY(BlueprintReadOnly, Category="Reactive") double ElectricalWetness01 = 0;
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
