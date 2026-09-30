#include "AetherAudioAuthoring.h"
#include "AetherAudioCatalog.h"
#include "Dom/JsonObject.h"
#include "Engine/AssetManager.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool UAetherAudioAuthoring::ImportGeneratedCatalog(UAetherAudioCatalog* Catalog, const FString& Json)
{
    if (!Catalog || !Catalog->Events.IsEmpty() || !Catalog->SourceDigest.IsEmpty()
        || !Catalog->GetPathName().StartsWith(TEXT("/Game/AetherAudio/Generated/"))) return false;
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) return false;
    FString Digest;
    if (!Root->TryGetStringField(TEXT("source_digest"), Digest) || Digest.Len() != 64) return false;
    for (TCHAR Char : Digest) if (!FChar::IsHexDigit(Char)) return false;
    const TArray<TSharedPtr<FJsonValue>>* Rows;
    if (!Root->TryGetArrayField(TEXT("events"), Rows) || Rows->IsEmpty()) return false;
    TArray<FAetherAudioEvent> Events;
    TSet<FName> Ids;
    for (const auto& Row : *Rows)
    {
        const TSharedPtr<FJsonObject>* Object;
        if (!Row->TryGetObject(Object) || !Object->IsValid()) return false;
        FAetherAudioEvent Event;
        FString Id;
        if (!(*Object)->TryGetStringField(TEXT("event_id"), Id) || Id.IsEmpty()) return false;
        Event.EventId = FName(Id);
        if (Ids.Contains(Event.EventId)) return false;
        Ids.Add(Event.EventId);
        const auto ReadFloat = [&Object](const TCHAR* Key, float& Value)
        {
            double Number;
            if (!(*Object)->TryGetNumberField(Key, Number) || !FMath::IsFinite(Number)) return false;
            Value = static_cast<float>(Number);
            return FMath::IsFinite(Value);
        };
        if (!ReadFloat(TEXT("gain"), Event.Gain) || !ReadFloat(TEXT("cooldown_seconds"), Event.CooldownSeconds)
            || !ReadFloat(TEXT("inner_radius_cm"), Event.InnerRadiusCm) || !ReadFloat(TEXT("falloff_distance_cm"), Event.FalloffDistanceCm)) return false;
        double Budget;
        if (!(*Object)->TryGetNumberField(TEXT("max_concurrent"), Budget) || !FMath::IsFinite(Budget)
            || Budget < 1 || Budget > 256 || Budget != static_cast<double>(static_cast<int32>(Budget))) return false;
        Event.MaxConcurrent = static_cast<int32>(Budget);
        if (!(*Object)->TryGetStringField(TEXT("unavailable_reason"), Event.UnavailableReason)) return false;
        const TArray<TSharedPtr<FJsonValue>>* Variants;
        if (!(*Object)->TryGetArrayField(TEXT("variants"), Variants)) return false;
        TSet<FName> Paths;
        for (const auto& Variant : *Variants)
        {
            FString Path;
            if (!Variant->TryGetString(Path) || !FPackageName::IsValidObjectPath(Path) || Paths.Contains(FName(Path))) return false;
            Paths.Add(FName(Path));
            Event.Variants.Add(TSoftObjectPtr<USoundBase>(FSoftObjectPath(Path)));
        }
        if (Event.UnavailableReason.IsEmpty())
        {
            if (!Event.IsPlayableDefinition()) return false;
        }
        else if (!Event.Variants.IsEmpty()) return false;
        Events.Add(MoveTemp(Event));
    }
    // Commit to the new, unpublished object only after validating every row.
    Catalog->Events = MoveTemp(Events);
    Catalog->SourceDigest = Digest;
    Catalog->PostEditChange();
    Catalog->MarkPackageDirty();
    return true;
}

FString UAetherAudioAuthoring::ExportGeneratedCatalog(const UAetherAudioCatalog* Catalog)
{
    if (!Catalog) return FString();
    auto Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("source_digest"), Catalog->SourceDigest);
    TArray<TSharedPtr<FJsonValue>> Events;
    for (const auto& Event : Catalog->Events)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("event_id"), Event.EventId.ToString());
        TArray<TSharedPtr<FJsonValue>> Variants;
        for (const auto& Variant : Event.Variants)
            Variants.Add(MakeShared<FJsonValueString>(Variant.ToSoftObjectPath().ToString()));
        Row->SetArrayField(TEXT("variants"), Variants);
        Events.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("events"), Events);
    FString Json;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

bool UAetherAudioAuthoring::IsCatalogRegistered(const UAetherAudioCatalog* Catalog)
{
    return Catalog && UAssetManager::Get().GetPrimaryAssetPath(Catalog->GetPrimaryAssetId())
        == FSoftObjectPath(Catalog);
}
