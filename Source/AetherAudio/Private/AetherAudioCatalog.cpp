#include "AetherAudioCatalog.h"
#include "Modules/ModuleManager.h"
IMPLEMENT_MODULE(FDefaultModuleImpl, AetherAudio)

namespace AetherAudioEvents
{
    const FName Break(TEXT("Aether.Audio.Reaction.Break"));
    const FName Freeze(TEXT("Aether.Audio.Reaction.Freeze"));
    const FName Extinguish(TEXT("Aether.Audio.Reaction.Extinguish"));
}

bool FAetherAudioEvent::IsPlayableDefinition() const
{
    if (EventId.IsNone() || !UnavailableReason.IsEmpty() || Variants.IsEmpty()
        || !FMath::IsFinite(Gain) || Gain <= 0.f
        || !FMath::IsFinite(CooldownSeconds) || CooldownSeconds < 0.f || MaxConcurrent < 1 || MaxConcurrent > 256
        || !FMath::IsFinite(InnerRadiusCm) || InnerRadiusCm < 0.f
        || !FMath::IsFinite(FalloffDistanceCm) || FalloffDistanceCm <= 0.f) return false;
    for (const auto& Variant : Variants) if (Variant.IsNull()) return false;
    return true;
}
FPrimaryAssetId UAetherAudioCatalog::GetPrimaryAssetId() const
{
    return FPrimaryAssetId(TEXT("AetherAudioCatalog"), GetFName());
}
bool UAetherAudioCatalog::HasUniqueEventIds() const
{
    TSet<FName> Seen;
    for (const auto& Event : Events)
    {
        if (Event.EventId.IsNone() || Seen.Contains(Event.EventId)) return false;
        Seen.Add(Event.EventId);
    }
    return !Events.IsEmpty();
}
