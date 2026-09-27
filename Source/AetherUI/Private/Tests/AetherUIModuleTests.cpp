#include "Misc/AutomationTest.h"
#include "Presentation/AetherPresentation.h"
#include "UI/AetherFrontierHUD.h"
#include "AetherFrontierPanel.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SoftObjectPath.h"
#include "Inspection/AetherInspectionWidgets.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherInspectionPressTest,"Aether.V10.Closure.DetailPressIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherInspectionPressTest::RunTest(const FString&)
{
    auto* Button=NewObject<UAetherInspectionActionButton>();
    FAetherInspectRequest Request;Request.Context={TEXT("Fixture"),FGuid::NewGuid(),1};Request.Target.InstanceId=FGuid::NewGuid();Request.DependencyKey=TEXT("item-a");
    FAetherInspectionAction Action;Action.Kind=EAetherInspectAction::Use;Action.bEnabled=true;
    int32 Calls=0;Button->OnRequested.AddLambda([&](const FAetherInspectRequest&,const FAetherInspectionAction&,bool){++Calls;});
    Button->InitializeAction(Request,Action);Button->OnPressed.Broadcast();
    ++Request.Context.SnapshotRevision;Button->InitializeAction(Request,Action);Button->OnClicked.Broadcast();
    TestEqual(TEXT("Unrelated refresh preserves held click"),Calls,1);
    Button->OnPressed.Broadcast();Request.Target.InstanceId=FGuid::NewGuid();Request.DependencyKey=TEXT("item-b");
    Button->InitializeAction(Request,Action);Button->OnClicked.Broadcast();
    TestEqual(TEXT("Changed object cancels held click"),Calls,1);
    Button->OnPressed.Broadcast();Action.bEnabled=false;Button->InitializeAction(Request,Action);
    Action.bEnabled=true;Button->InitializeAction(Request,Action);Button->OnClicked.Broadcast();
    TestEqual(TEXT("Disable then enable does not resurrect held click"),Calls,1);
    Button->OnPressed.Broadcast();Button->OnClicked.Broadcast();
    TestEqual(TEXT("Fresh click after refresh works"),Calls,2);
    Button->OnRequested.Clear();return true;
}
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
