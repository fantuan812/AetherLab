#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReactiveMechanismComponent.generated.h"
class UPrimitiveComponent;
class UReactiveBodyComponent;
class UPhysicsConstraintComponent;

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
    UPROPERTY(EditAnywhere,Category="Mechanism") bool bImpactDamage = false;
    UPROPERTY(EditAnywhere,Category="Mechanism") double ImpactThresholdJ = 80;
    UPROPERTY(EditAnywhere,Category="Power") double PowerW = 0;
    UPROPERTY(Replicated,EditAnywhere,BlueprintReadOnly,Category="Power") double RemainingEnergyJ = 0;
    UPROPERTY(Replicated,EditAnywhere,BlueprintReadOnly,Category="Power") bool bPowerEnabled = true;
    UPROPERTY(EditAnywhere,Category="Power") double LifetimeSeconds = 0; // Zero: no lifetime cap; energy still finite.
    UPROPERTY(Replicated) double SourceAge = 0;
    void RestoreMechanism(bool Released, double EnergyJ, double Age, bool Enabled);
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    void ReleaseSupport();
    UFUNCTION() void Hit(UPrimitiveComponent* HitComponent,AActor* Other,UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Result);
    TMap<TWeakObjectPtr<AActor>,double> LastImpacts;
    uint64 PulseSequence = 0;
    double PowerAccumulator = 0;
    TWeakObjectPtr<UPrimitiveComponent> Primitive;
};
