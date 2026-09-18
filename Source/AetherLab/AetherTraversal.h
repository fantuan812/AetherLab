#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AetherTraversal.generated.h"
class ANavLinkProxy;
UCLASS(ClassGroup=(Aether),meta=(BlueprintSpawnableComponent))
class UAetherTraversalComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UAetherTraversalComponent();
 UPROPERTY(EditAnywhere) bool bAuthoredBridge=false;
 UPROPERTY(EditAnywhere) FVector StartLocal=FVector(-42,0,60);
 UPROPERTY(EditAnywhere) FVector EndLocal=FVector(42,0,60);
 UPROPERTY(EditAnywhere) float SettleSeconds=.75f;
 UPROPERTY(EditAnywhere) float MaxSpeedCm=8;
 UPROPERTY(Replicated) bool bRouteOpen=false;
 UPROPERTY(Replicated) uint32 Revision=0;
 virtual void BeginPlay() override;
 virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
 UPROPERTY() TObjectPtr<ANavLinkProxy> Link;
 float StableSeconds=0;
 FTransform LastPose;
 void SetOpen(bool Open);
};
