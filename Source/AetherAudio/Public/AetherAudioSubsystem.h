#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AetherAudioCatalog.h"
#include "AetherAudioSubsystem.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundConcurrency;

UENUM(BlueprintType)
enum class EAetherAudioResult : uint8
{
    Started, // Component accepted by the audio system; does not promise perceptual audibility.
    Suppressed,
    Unavailable
};

USTRUCT()
struct FAetherLoadedAudioEvent
{
    GENERATED_BODY()
    UPROPERTY() TArray<TObjectPtr<USoundBase>> Sounds;
    UPROPERTY() TObjectPtr<USoundAttenuation> Attenuation;
    UPROPERTY() TObjectPtr<USoundConcurrency> Concurrency;
    TArray<TWeakObjectPtr<UAudioComponent>> Active;
    double NextAllowedTime = 0.;
    int32 NextVariant = 0;
};

UCLASS()
class AETHERAUDIO_API UAetherAudioSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable, Category="Audio") EAetherAudioResult PlayEvent(FName EventId, FVector Location);
private:
    UPROPERTY(Transient) TObjectPtr<UAetherAudioCatalog> Catalog;
    UPROPERTY(Transient) TMap<FName, FAetherLoadedAudioEvent> Loaded;
};
