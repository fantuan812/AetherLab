#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReactiveMechanismComponent.h"
#include "AetherPhysicsDamage.generated.h"

USTRUCT(BlueprintType)
struct FAetherImpactDamagePolicy
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float ThresholdJ=80;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MinClosingMPerSecond=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float JoulesPerDamage=35;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MaxDamage=70;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float PosturePerDamage=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float ReceiverCooldownSeconds=.5f;
    bool IsValid() const;
    float DamageFor(double EnergyJ) const;
};

// Gameplay adapter: ReactiveRuntime reports facts and never chooses health/posture balance.
UCLASS(ClassGroup=(Aether),meta=(BlueprintSpawnableComponent))
class UAetherPhysicsDamageComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,Category="Impact") FAetherImpactDamagePolicy Policy;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void ReceiveImpact(const FReactiveImpactEvent& Event);
    void DispatchImpact(const FReactiveImpactEvent& Event,float Damage);
    TWeakObjectPtr<UReactiveMechanismComponent> Mechanism;
    FDelegateHandle ImpactDelegate;
    uint64 LastEventId=0;
    TMap<TWeakObjectPtr<AActor>,double> LastDamageAt;
};
