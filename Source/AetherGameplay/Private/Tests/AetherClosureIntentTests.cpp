#include "Misc/AutomationTest.h"
#include "Inspection/AetherInspectionService.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherClosureIntentTest,"Aether.V10.Closure.UnsentInventoryIntent",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherClosureIntentTest::RunTest(const FString&)
{
    FAetherInspectionSnapshot S;S.Context={TEXT("ClosureFixture"),FGuid::NewGuid(),1};S.bCanAct=true;S.ProfileRevision=1;S.WorldRevision=1;S.Inventory.Capacity=8;
    FAetherV10ItemInstance Item;Item.InstanceId=FGuid::NewGuid();Item.DefinitionId=TEXT("Potion");Item.Quantity=2;Item.SlotIndex=0;S.Inventory.Items.Add(Item);S.RebuildLookup();
    FAetherInspectTarget Source;Source.InstanceId=Item.InstanceId;Source.SlotId=TEXT("0");
    FAetherInspectTarget Target;Target.SlotId=TEXT("1");
    const auto From=AetherInspection::Pin(S,Source),To=AetherInspection::Pin(S,Target);FAetherInspectRequest Current;
    ++S.WorldRevision;++S.Context.SnapshotRevision;
    TestTrue(TEXT("Unrelated world display revision preserves unsent intent"),AetherInspection::RevalidateIntent(S,From,Current));
    TestEqual(TEXT("Presentation context advances after validation"),Current.Context.SnapshotRevision,S.Context.SnapshotRevision);
    ++S.ProfileRevision;
    TestTrue(TEXT("Unrelated profile update preserves source"),AetherInspection::RevalidateIntent(S,From,Current));
    S.Inventory.Items[0].Quantity=1;S.RebuildLookup();
    TestFalse(TEXT("Actual source quantity change invalidates"),AetherInspection::RevalidateIntent(S,From,Current));
    S.Inventory.Items[0]=Item;auto Occupant=Item;Occupant.InstanceId=FGuid::NewGuid();Occupant.SlotIndex=1;S.Inventory.Items.Add(Occupant);S.RebuildLookup();
    TestFalse(TEXT("Target claimed after preview invalidates"),AetherInspection::RevalidateIntent(S,To,Current));
    S.Inventory.Items[0].bLocked=true;S.RebuildLookup();
    TestFalse(TEXT("Source lock change invalidates"),AetherInspection::RevalidateIntent(S,From,Current));
    S.Inventory.Items[0]=Item;S.Inventory.Items[0].SlotIndex=2;S.RebuildLookup();
    TestFalse(TEXT("Source moved from selected slot invalidates"),AetherInspection::RevalidateIntent(S,From,Current));
    S.Inventory.Items[0]=Item;S.RebuildLookup();S.Context.SessionId=FGuid::NewGuid();
    TestFalse(TEXT("Replacement pawn session invalidates"),AetherInspection::RevalidateIntent(S,From,Current));
    S.Container.Emplace();S.Container->ContainerId=TEXT("FixtureBox");S.Container->bActive=true;S.Container->Inventory.Items.Add(Item);S.ContainerContext=FGuid::NewGuid();S.RebuildLookup();
    Source.ContainerId=TEXT("FixtureBox");const auto Box=AetherInspection::Pin(S,Source);
    TestTrue(TEXT("Authorized container source valid"),AetherInspection::RevalidateIntent(S,Box,Current));
    S.ContainerContext=FGuid::NewGuid();
    TestFalse(TEXT("Container authorization replaced invalidates"),AetherInspection::RevalidateIntent(S,Box,Current));
    S.Container.Reset();S.ContainerContext.Invalidate();S.RebuildLookup();
    TestFalse(TEXT("Closed container invalidates"),AetherInspection::RevalidateIntent(S,Box,Current));
    return true;
}
#endif
