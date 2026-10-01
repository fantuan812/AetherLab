#include "Misc/AutomationTest.h"
#include "Inventory/AetherInventoryCell.h"
#include "Input/DragAndDrop.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherInventoryDragLifecycleTest,"Aether.V10.UI.InventoryDragPressIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherInventoryDragLifecycleTest::RunTest(const FString&)
{
    auto* Cell=NewObject<UAetherInventoryCell>();
    const TSet<FKey> Held{EKeys::LeftMouseButton},Released;
    const FPointerEvent Mouse(0u,0u,FVector2D::ZeroVector,FVector2D::ZeroVector,Held,EKeys::LeftMouseButton,0.f,FModifierKeysState());
    const FPointerEvent MouseUp(0u,0u,FVector2D::ZeroVector,FVector2D::ZeroVector,Released,EKeys::LeftMouseButton,0.f,FModifierKeysState());
    const FPointerEvent OtherPointer(0u,1u,FVector2D::ZeroVector,FVector2D::ZeroVector,Held,EKeys::LeftMouseButton,0.f,FModifierKeysState());
    const FPointerEvent OtherUser(1u,0u,FVector2D::ZeroVector,FVector2D::ZeroVector,Held,EKeys::LeftMouseButton,0.f,FModifierKeysState());
    const FPointerEvent Touch(0u,0u,FVector2D::ZeroVector,FVector2D::ZeroVector,1.f,true,false,false,FModifierKeysState());
    FAetherInspectionSnapshot Snapshot;Snapshot.Context={TEXT("DragOwner"),FGuid::NewGuid(),4};Snapshot.ProfileRevision=3;
    FAetherV10ItemInstance Item;Item.InstanceId=FGuid::NewGuid();Item.DefinitionId=TEXT("Fixture.Item");Item.SlotIndex=0;Item.Quantity=2;
    Snapshot.Inventory.Items.Add(Item);Snapshot.RebuildLookup();
    FAetherInspectTarget Target;Target.InstanceId=Item.InstanceId;Target.SlotId=TEXT("0");
    const auto Original=AetherInspection::Pin(Snapshot,Target);
    const auto Present=[&](const FAetherInspectRequest& Request,int32 Slot=0,bool Filtered=false)
    {Cell->Present(Request,Slot,TEXT("fixture"),FString(),Filtered,false);};
    // 调用鼠标生产入口使用的捕获/消费函数；不另造身份门，不模拟Slate的阈值或焦点路由。
    const auto Press=[&]() {Present(Original);Cell->CaptureDragSource(Mouse);};
    FAetherInspectRequest Source;
    Present(Original);
    TestFalse(TEXT("Detection without a press cannot invent a source"),Cell->ConsumeDragSource(Mouse,Source));
    Press();++Snapshot.Context.SnapshotRevision;++Snapshot.ProfileRevision;
    const auto Refreshed=AetherInspection::Pin(Snapshot,Target);Present(Refreshed);
    TestTrue(TEXT("Same object survives an unrelated snapshot version"),Cell->ConsumeDragSource(Mouse,Source));
    TestTrue(TEXT("Consumed value retains the original pressed context"),Source.Context.Same(Original.Context)&&Source.DependencyKey==Original.DependencyKey);
    TestFalse(TEXT("One press can start only one drag"),Cell->ConsumeDragSource(Mouse,Source));

    Press();auto Replacement=Snapshot;Replacement.Inventory.Items[0].InstanceId=FGuid::NewGuid();Replacement.RebuildLookup();
    auto ReplacementTarget=Target;ReplacementTarget.InstanceId=Replacement.Inventory.Items[0].InstanceId;
    Present(AetherInspection::Pin(Replacement,ReplacementTarget));
    UDragDropOperation* Operation=nullptr;Cell->NativeOnDragDetected(FGeometry(),Mouse,Operation);
    TestNull(TEXT("Real detection rejects a replacement object in the same cell"),Operation);
    Present(Original);TestFalse(TEXT("Returning to the old object cannot resurrect the press"),Cell->ConsumeDragSource(Mouse,Source));
    Press();auto Changed=Snapshot;++Changed.Inventory.Items[0].Quantity;Changed.RebuildLookup();
    Present(AetherInspection::Pin(Changed,Target));
    TestFalse(TEXT("Changed item dependency cancels the held source"),Cell->ConsumeDragSource(Mouse,Source));

    for(int32 Case=0;Case<7;++Case)
    {
        Press();auto Different=Original;int32 Slot=0;
        if(Case==0)Different.Context.OwnerIdentity=TEXT("dragowner");
        if(Case==1)Different.Context.SessionId=FGuid::NewGuid();
        if(Case==2)Different.Target.SlotId=TEXT("1");
        if(Case==3)Different.Target.ContainerId=TEXT("another-container");
        if(Case==4)Different.Target.Kind=EAetherInspectTarget::EquipmentSlot;
        if(Case==5)Different.Target.DefinitionId=TEXT("Fixture.Other");
        if(Case==6)Slot=1;
        Present(Different,Slot);Present(Original);
        TestFalse(TEXT("Owner/session/slot/container/kind/definition changes cannot revive old input"),Cell->ConsumeDragSource(Mouse,Source));
    }
    Press();Present(Original,0,true);Present(Original);
    TestFalse(TEXT("Filtering out and restoring a cell cannot revive old input"),Cell->ConsumeDragSource(Mouse,Source));
    Press();Cell->SetIsEnabled(false);
    TestFalse(TEXT("Disabled cell cannot start dragging"),Cell->ConsumeDragSource(Mouse,Source));Cell->SetIsEnabled(true);
    Press();Cell->NativeOnMouseButtonUp(FGeometry(),MouseUp);
    TestFalse(TEXT("Mouse release cancels the pending source"),Cell->ConsumeDragSource(Mouse,Source));
    Press();Cell->NativeOnMouseCaptureLost(FCaptureLostEvent(0,0));
    TestFalse(TEXT("Capture loss cancels the pending source"),Cell->ConsumeDragSource(Mouse,Source));
    Press();Cell->NativeOnRemovedFromFocusPath(FFocusEvent(EFocusCause::Navigation,0));
    Cell->NativeOnAddedToFocusPath(FFocusEvent(EFocusCause::Navigation,0));
    TestFalse(TEXT("Focus leaves and returns without reviving a press that never owned capture"),Cell->ConsumeDragSource(Mouse,Source));
    Press();Cell->NativeOnMouseLeave(Mouse);
    TestTrue(TEXT("Leaving a cell alone does not cancel a held cross-cell drag"),Cell->ConsumeDragSource(Mouse,Source));
    Press();Cell->NativeOnDragCancelled(FDragDropEvent(Mouse,TSharedPtr<FDragDropOperation>()),nullptr);
    TestFalse(TEXT("Drag cancellation cannot leave a reusable source"),Cell->ConsumeDragSource(Mouse,Source));
    Press();Cell->NativeDestruct();Present(Original);
    TestFalse(TEXT("Close and recreate cannot reuse the old press"),Cell->ConsumeDragSource(Mouse,Source));
    Press();Cell->ReleaseSlateResources(true);Present(Original);
    TestFalse(TEXT("Slate reconstruction cannot reuse the old press"),Cell->ConsumeDragSource(Mouse,Source));

    Press();TestFalse(TEXT("Mouse button must still be held at detection"),Cell->ConsumeDragSource(MouseUp,Source));
    Press();TestFalse(TEXT("Another pointer cannot consume this press"),Cell->ConsumeDragSource(OtherPointer,Source));
    Press();TestFalse(TEXT("Another Slate user cannot consume this press"),Cell->ConsumeDragSource(OtherUser,Source));
    Press();TestFalse(TEXT("Touch cannot borrow the stored mouse press"),Cell->ConsumeDragSource(Touch,Source));
    Present(Original);Cell->CaptureDragSource(Touch);
    TestTrue(TEXT("A separately captured matching touch retains its own identity"),Cell->ConsumeDragSource(Touch,Source));
    int32 KeyboardPickups=0;
    Cell->OnIntent.BindLambda([&](const FAetherInspectRequest& Request,int32,EAetherCellIntent Intent)
    {if(Intent==EAetherCellIntent::PickUp&&Request.Target.InstanceId==Item.InstanceId)++KeyboardPickups;});
    Cell->NativeOnKeyDown(FGeometry(),FKeyEvent(EKeys::Gamepad_FaceButton_Left,FModifierKeysState(),0,false,0,0));
    TestEqual(TEXT("Gamepad pickup still uses its original intent route"),KeyboardPickups,1);
    TestFalse(TEXT("Gamepad pickup does not manufacture a mouse source"),Cell->ConsumeDragSource(Mouse,Source));
    Press();TestTrue(TEXT("Fresh press after cancellation can start again"),Cell->ConsumeDragSource(Mouse,Source));
    Cell->OnIntent.Unbind();Cell->ReleaseSlateResources(true);return true;
}
#endif
