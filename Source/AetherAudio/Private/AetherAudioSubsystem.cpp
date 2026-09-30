#include "AetherAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"

DEFINE_LOG_CATEGORY_STATIC(LogAetherAudio, Log, All);

bool UAetherAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld()
        && !IsRunningDedicatedServer() && World->GetNetMode() != NM_DedicatedServer;
}

void UAetherAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    if (IsRunningDedicatedServer() || InWorld.GetNetMode() == NM_DedicatedServer) return;
    // Resolve once at world startup; reaction dispatch performs no disk loads.
    const auto Id = GetDefault<UAetherAudioSettings>()->CatalogId;
    Catalog = Cast<UAetherAudioCatalog>(UAssetManager::Get().GetPrimaryAssetPath(Id).TryLoad());
    if (!Catalog || !Catalog->HasUniqueEventIds())
    {
        UE_LOG(LogAetherAudio, Warning, TEXT("Audio unavailable: missing/invalid catalog %s"), *Id.ToString());
        Catalog = nullptr;
        return;
    }
    for (const auto& Definition : Catalog->Events)
    {
        if (!Definition.IsPlayableDefinition())
        {
            UE_LOG(LogAetherAudio, Warning, TEXT("Audio event unavailable: %s (%s)"), *Definition.EventId.ToString(), *Definition.UnavailableReason);
            continue;
        }
        FAetherLoadedAudioEvent Entry;
        for (const auto& Variant : Definition.Variants)
        {
            USoundBase* Sound = Variant.LoadSynchronous();
            if (!Sound || Sound->IsLooping()) { Entry.Sounds.Reset(); break; }
            Entry.Sounds.Add(Sound);
        }
        if (Entry.Sounds.Num() != Definition.Variants.Num())
        {
            UE_LOG(LogAetherAudio, Warning, TEXT("Audio event unavailable: failed variant for %s"), *Definition.EventId.ToString());
            continue;
        }
        Entry.Attenuation = NewObject<USoundAttenuation>(this);
        Entry.Attenuation->Attenuation.bAttenuate = true;
        Entry.Attenuation->Attenuation.bSpatialize = true;
        Entry.Attenuation->Attenuation.AttenuationShapeExtents.X = Definition.InnerRadiusCm;
        Entry.Attenuation->Attenuation.FalloffDistance = Definition.FalloffDistanceCm;
        Entry.Concurrency = NewObject<USoundConcurrency>(this);
        Entry.Concurrency->Concurrency.SetEnableMaxCountPlatformScaling(false);
        if (!Entry.Concurrency->Concurrency.SetMaxCount(Definition.MaxConcurrent))
        {
            UE_LOG(LogAetherAudio, Warning, TEXT("Audio event unavailable: concurrency setup failed for %s"), *Definition.EventId.ToString());
            continue;
        }
        Entry.Concurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::PreventNew;
        Loaded.Add(Definition.EventId, MoveTemp(Entry));
    }
}

EAetherAudioResult UAetherAudioSubsystem::PlayEvent(FName EventId, FVector Location)
{
    UWorld* World = GetWorld();
    if (!World || IsRunningDedicatedServer() || World->GetNetMode() == NM_DedicatedServer
        || !World->AllowAudioPlayback() || Location.ContainsNaN() || !Catalog) return EAetherAudioResult::Unavailable;
    auto* Entry = Loaded.Find(EventId);
    const auto* Definition = Catalog->Events.FindByPredicate([EventId](const auto& Event) { return Event.EventId == EventId; });
    if (!Entry || !Definition || Entry->Sounds.IsEmpty()) return EAetherAudioResult::Unavailable;
    Entry->Active.RemoveAll([](const auto& Component) { return !Component.IsValid() || !Component->IsPlaying(); });
    const double Now = World->GetTimeSeconds();
    if (Now < Entry->NextAllowedTime || Entry->Active.Num() >= Definition->MaxConcurrent) return EAetherAudioResult::Suppressed;
    USoundBase* Sound = Entry->Sounds[Entry->NextVariant];
    if (!Sound) return EAetherAudioResult::Unavailable;
    UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(World, Sound, Location,
        FRotator::ZeroRotator, Definition->Gain, 1.f, 0.f, Entry->Attenuation, Entry->Concurrency, true);
    if (!Component) return EAetherAudioResult::Unavailable;
    if (!Component->IsPlaying())
    {
        Component->DestroyComponent();
        return EAetherAudioResult::Unavailable;
    }
    Entry->Active.Add(Component);
    Entry->NextAllowedTime = Now + Definition->CooldownSeconds;
    Entry->NextVariant = (Entry->NextVariant + 1) % Entry->Sounds.Num();
    return EAetherAudioResult::Started;
}

void UAetherAudioSubsystem::Deinitialize()
{
    for (auto& Pair : Loaded)
        for (auto& Component : Pair.Value.Active)
            if (Component.IsValid()) Component->Stop();
    Loaded.Reset();
    Catalog = nullptr;
    Super::Deinitialize();
}
