#if WITH_DEV_AUTOMATION_TESTS
#include "AetherAudioCatalog.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherAudioDefinitionTest, "Aether.Audio.DefinitionContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAetherAudioDefinitionTest::RunTest(const FString&)
{
    FAetherAudioEvent Event;
    TestFalse(TEXT("Empty event unavailable"), Event.IsPlayableDefinition());
    Event.EventId = AetherAudioEvents::Break;
    Event.FalloffDistanceCm = 900.f;
    Event.Variants.Add(TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Test/Sound.Sound"))));
    TestTrue(TEXT("Complete definition is eligible, not proof of loaded asset"), Event.IsPlayableDefinition());
    Event.UnavailableReason = TEXT("Source blocked");
    TestFalse(TEXT("Explicit unavailable overrides references"), Event.IsPlayableDefinition());
    Event.UnavailableReason.Reset();
    Event.Variants.Add(TSoftObjectPtr<USoundBase>());
    TestFalse(TEXT("Null variant rejected"), Event.IsPlayableDefinition());
    Event.Variants.Pop();
    Event.CooldownSeconds = -1.f;
    TestFalse(TEXT("Negative cooldown rejected"), Event.IsPlayableDefinition());
    Event.CooldownSeconds = 0.f;
    Event.MaxConcurrent = 0;
    TestFalse(TEXT("Zero budget rejected"), Event.IsPlayableDefinition());
    Event.MaxConcurrent = 2;
    UAetherAudioCatalog* Catalog = NewObject<UAetherAudioCatalog>();
    Catalog->Events.Add(Event);
    TestTrue(TEXT("Unique ID"), Catalog->HasUniqueEventIds());
    Catalog->Events.Add(Event);
    TestFalse(TEXT("Duplicate ID fails whole catalog"), Catalog->HasUniqueEventIds());
    return true;
}
#endif
