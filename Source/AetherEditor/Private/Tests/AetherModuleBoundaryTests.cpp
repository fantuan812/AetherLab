#include "Misc/AutomationTest.h"
#include "AetherWorldAuthoring.h"
#include "World/AetherShellDefinition.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherEditorBoundaryTest,
    "Aether.V10.Modules.AuthoringRedirectAndShell",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAetherEditorBoundaryTest::RunTest(const FString&)
{
    // 旧工具资产若仍引用旧脚本路径，必须解析为同一个新类，而非静默丢失节点。
    UClass* Legacy = LoadObject<UClass>(nullptr, TEXT("/Script/AetherLab.AetherWorldAuthoring"));
    TestTrue(TEXT("Exact legacy authoring path resolves"), Legacy == UAetherWorldAuthoring::StaticClass());
    TestEqual(TEXT("Authoring belongs to editor only"),
        UAetherWorldAuthoring::StaticClass()->GetOutermost()->GetName(), FString(TEXT("/Script/AetherEditor")));
    TSet<FName> Ids;
    for (const auto& Piece : AetherShell::Pieces())
    {
        TestFalse(TEXT("Shell IDs are unique"), Ids.Contains(Piece.Id));
        Ids.Add(Piece.Id);
        TestTrue(TEXT("Runtime skip set equals authored set"), AetherShell::IsShellPiece(Piece.Id));
        TestFalse(TEXT("Shell transforms are finite"), Piece.Location.ContainsNaN() || Piece.Scale.ContainsNaN());
    }
    TestEqual(TEXT("Original shell geometry count retained"), Ids.Num(), 44);
    TestFalse(TEXT("Interactive mechanism is not a shell piece"), AetherShell::IsShellPiece(TEXT("WorksRope")));
    return true;
}
#endif
