#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "Sound/SoundBase.h"
#include "AetherAudioCatalog.generated.h"

// Stable presentation contracts. No gameplay state, asset paths or mix values live here.
namespace AetherAudioEvents
{
    AETHERAUDIO_API extern const FName Break;
    AETHERAUDIO_API extern const FName Freeze;
    AETHERAUDIO_API extern const FName Extinguish;
}

USTRUCT(BlueprintType)
struct AETHERAUDIO_API FAetherAudioEvent
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName EventId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta=(AssetBundles="Audio")) TArray<TSoftObjectPtr<USoundBase>> Variants;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Gain = 1.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float CooldownSeconds = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 MaxConcurrent = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float InnerRadiusCm = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float FalloffDistanceCm = 0.f;
    // Non-empty blocks playback, even if old imported variants still exist.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString UnavailableReason;
    bool IsPlayableDefinition() const;
};

// Generated from ContentSource/Audio/catalog.json; edit that source, then reimport.
UCLASS(BlueprintType)
class AETHERAUDIO_API UAetherAudioCatalog : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FAetherAudioEvent> Events;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString SourceDigest;
    virtual FPrimaryAssetId GetPrimaryAssetId() const override;
    bool HasUniqueEventIds() const;
};

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Aether Audio"))
class AETHERAUDIO_API UAetherAudioSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Catalog") FPrimaryAssetId CatalogId;
};
