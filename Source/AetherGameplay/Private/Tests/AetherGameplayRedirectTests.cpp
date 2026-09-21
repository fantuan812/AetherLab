#include "Misc/AutomationTest.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Framework/AetherFrontierMode.h"
#include "Persistence/AetherFrontierSave.h"
#include "Animation/AetherAnimation.h"
#include "UObject/SoftObjectPath.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherGameplayRedirectTest,"Aether.V10.Modules.GameplayRedirects",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherGameplayRedirectTest::RunTest(const FString&)
{
    const TPair<const TCHAR*,UClass*> Classes[]={
        {TEXT("AetherFrontierCharacter"),AAetherFrontierCharacter::StaticClass()},
        {TEXT("AetherFrontierMode"),AAetherFrontierMode::StaticClass()},
        {TEXT("AetherFrontierSave"),UAetherFrontierSave::StaticClass()},
        {TEXT("AetherAnimInstance"),UAetherAnimInstance::StaticClass()}
    };
    for(const auto& Entry:Classes)
    {
        FSoftClassPath Old(FString(TEXT("/Script/AetherLab."))+Entry.Key);
        TestTrue(TEXT("Exact old reflection path redirects"),Old.FixupCoreRedirects());
        TestTrue(TEXT("Existing asset class resolves to same implementation"),Old.TryLoadClass<UObject>()==Entry.Value);
        TestEqual(TEXT("Gameplay no longer belongs to assembly module"),Entry.Value->GetOutermost()->GetName(),FString(TEXT("/Script/AetherGameplay")));
    }
    return true;
}
#endif
