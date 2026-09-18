#include "Misc/AutomationTest.h"
#include "Presentation/AetherPresentation.h"
#include "UI/AetherFrontierHUD.h"
#include "AetherFrontierPanel.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherUIBoundaryTest, "Aether.V10.Modules.ClientPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAetherUIBoundaryTest::RunTest(const FString&)
{
    TestTrue(TEXT("Client HUD provider registered"), AetherPresentation::ResolveHUD() == AAetherFrontierHUD::StaticClass());
    const TPair<const TCHAR*, UClass*> Cases[] {
        {TEXT("/Script/AetherLab.AetherFrontierHUD"), AAetherFrontierHUD::StaticClass()},
        {TEXT("/Script/AetherLab.AetherFrontierPanel"), UAetherFrontierPanel::StaticClass()},
        {TEXT("/Script/AetherLab.AetherFrontierViewModel"), UAetherFrontierViewModel::StaticClass()}
    };
    for (const auto& Entry : Cases)
    {
        // 精确覆盖旧蓝图可能保留的三个脚本类引用。
        FSoftClassPath LegacyPath(Entry.Key);
        TestTrue(TEXT("Exact UI class redirect registered"), LegacyPath.FixupCoreRedirects());
        TestTrue(Entry.Key, LegacyPath.TryLoadClass<UObject>() == Entry.Value);
        TestEqual(TEXT("UI class is in client module"), Entry.Value->GetOutermost()->GetName(), FString(TEXT("/Script/AetherUI")));
    }
    return true;
}
#endif
