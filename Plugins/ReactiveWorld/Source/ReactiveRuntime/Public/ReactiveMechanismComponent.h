#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReactiveMechanismComponent.generated.h"
class UPrimitiveComponent;
class UReactiveBodyComponent;
class UPhysicsConstraintComponent;

// Authoritative physical observation. Energy is an impulse-derived proxy, not health damage.
// EventId is monotonic within this mechanism lifetime, never a persistent player identifier.
struct REACTIVERUNTIME_API FReactiveImpactEvent
{
    TWeakObjectPtr<AActor> Mechanism,Receiver,Source;
    FName MechanismId,ReceiverId;
    uint64 EventId=0;
    double TimeSeconds=0,EnergyJ=0,RelativeClosingMPerSec=0;
    bool bSustainedContact=false;
    FVector ImpulseNs=FVector::ZeroVector,PositionCm=FVector::ZeroVector;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FReactiveImpactObserved,const FReactiveImpactEvent&);

// Authored mechanisms only: supports, hinge/rope constraints and finite power sources.
UCLASS(ClassGroup=(Reactive),meta=(BlueprintSpawnableComponent))
class REACTIVERUNTIME_API UReactiveMechanismComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UReactiveMechanismComponent();
    UPROPERTY(EditAnywhere,Category="Mechanism") TArray<TObjectPtr<UReactiveBodyComponent>> Supports;
    UPROPERTY(EditAnywhere,Category="Mechanism") TObjectPtr<UPhysicsConstraintComponent> Constraint;
    UPROPERTY(EditAnywhere,Category="Mechanism") bool bReleaseWhenAnySupportBreaks = true;
    UPROPERTY(Replicated,EditAnywhere,Category="Mechanism") bool bGateOpen = false;
    UPROPERTY(EditAnywhere,Category="Mechanism") bool bBuoyant = false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Mechanism") bool bReleased = false;
    UPROPERTY(EditAnywhere,Category="Mechanism") bool bReportImpacts = false;
    FReactiveImpactObserved OnImpact;
    UPROPERTY(EditAnywhere,Category="Power") double PowerW = 0;
    UPROPERTY(Replicated,EditAnywhere,BlueprintReadOnly,Category="Power") double RemainingEnergyJ = 0;
    UPROPERTY(Replicated,EditAnywhere,BlueprintReadOnly,Category="Power") bool bPowerEnabled = true;
    UPROPERTY(EditAnywhere,Category="Power") double LifetimeSeconds = 0; // Zero: no lifetime cap; energy still finite.
    UPROPERTY(Replicated) double SourceAge = 0;
    UPROPERTY(EditAnywhere,Category="Mechanism",meta=(ClampMin="0",ClampMax="30")) double ImpactCreditSeconds = 5;
    void RecordImpactSource(AActor* Source);
    void AdvancePower(double FixedStepSeconds);
    AActor* GetImpactSource() const;
    void RestoreMechanism(bool Released, double EnergyJ, double Age, bool Enabled);
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    void ReleaseSupport();
    UFUNCTION() void Hit(UPrimitiveComponent* HitComponent,AActor* Other,UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Result);
    uint64 ImpactSequence = 0;
    uint64 PulseSequence = 0;
    TWeakObjectPtr<AActor> ImpactSource;
    double ImpactSourceExpiresAt = -1;
    TWeakObjectPtr<UPrimitiveComponent> Primitive;
    FVector PrePhysicsLinear=FVector::ZeroVector,PrePhysicsAngular=FVector::ZeroVector,PrePhysicsCenter=FVector::ZeroVector;
};
