#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AetherProjectile.generated.h"
class UStaticMeshComponent;
namespace Reactive {struct FEnvironment;}
UCLASS()
class AETHERLAB_API AAetherProjectile : public AActor
{
    GENERATED_BODY()
public:
    AAetherProjectile();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(Replicated) double HeatJ = 60000;
    FVector VelocityCm = FVector::ZeroVector;
    float Age = 0;
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    static void IntegrateWeather(double& Heat, FVector& Velocity, const Reactive::FEnvironment& Weather, float Dt);
};
