#include "Inventory/AetherInventoryCell.h"
#include "UI/AetherWidgetAssets.h"
#include "UI/AetherUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Engine/Texture2D.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "InputCoreTypes.h"

namespace
{
bool SameDragSource(const FAetherInspectRequest& A,const FAetherInspectRequest& B)
{
    // SnapshotRevision 可因其他格或倒计时更新；对象依赖与拥有者身份必须保持精确一致。
    return A.Context.IsValid()&&B.Context.IsValid()&&A.Context.SessionId==B.Context.SessionId&&
        A.Context.OwnerIdentity.Equals(B.Context.OwnerIdentity,ESearchCase::CaseSensitive)&&
        A.Target.Kind==B.Target.Kind&&A.Target.InstanceId==B.Target.InstanceId&&
        A.Target.SlotId.Equals(B.Target.SlotId,ESearchCase::CaseSensitive)&&
        A.Target.ContainerId.Equals(B.Target.ContainerId,ESearchCase::CaseSensitive)&&
        A.Target.DefinitionId.Equals(B.Target.DefinitionId,ESearchCase::CaseSensitive)&&
        !A.DependencyKey.IsEmpty()&&A.DependencyKey.Equals(B.DependencyKey,ESearchCase::CaseSensitive);
}
}

TSharedRef<SWidget> UAetherInventoryCell::RebuildWidget()
{
    SetIsFocusable(true);if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(WidgetTree->RootWidget)AetherWidgetAssets::BindDesigner(*this,*WidgetTree);
    if(!WidgetTree->RootWidget)
    {
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetMinDesiredWidth(74);Size->SetMinDesiredHeight(80);WidgetTree->RootWidget=Size;
        Background=WidgetTree->ConstructWidget<UBorder>();Background->SetPadding(FMargin(5));Size->SetContent(Background);
        auto* Rows=WidgetTree->ConstructWidget<UVerticalBox>();Background->SetContent(Rows);
        Icon=WidgetTree->ConstructWidget<UImage>();Icon->SetDesiredSizeOverride(FVector2D(32,32));Rows->AddChildToVerticalBox(Icon);
        Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetAutoWrapText(true);Rows->AddChildToVerticalBox(Label);
    }
    // 空格首次绘制也隐藏无资源图片，避免 UImage 默认白色刷块。
    if(Icon&&ShownIcon.IsEmpty())Icon->SetVisibility(ESlateVisibility::Collapsed);
    if(!Decorations)
    {
        auto* Body=WidgetTree->RootWidget.Get();Decorations=WidgetTree->ConstructWidget<UOverlay>();WidgetTree->RootWidget=Decorations;
        auto* BodySlot=Decorations->AddChildToOverlay(Body);BodySlot->SetHorizontalAlignment(HAlign_Fill);BodySlot->SetVerticalAlignment(VAlign_Fill);
        Quantity=WidgetTree->ConstructWidget<UTextBlock>();Quantity->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* CountSlot=Decorations->AddChildToOverlay(Quantity);CountSlot->SetHorizontalAlignment(HAlign_Right);CountSlot->SetVerticalAlignment(VAlign_Top);CountSlot->SetPadding(FMargin(4));
        Badges=WidgetTree->ConstructWidget<UTextBlock>();Badges->SetAutoWrapText(true);Badges->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto Font=Badges->GetFont();Font.Size=10;Badges->SetFont(Font);
        auto* BadgeSlot=Decorations->AddChildToOverlay(Badges);BadgeSlot->SetHorizontalAlignment(HAlign_Left);BadgeSlot->SetVerticalAlignment(VAlign_Bottom);BadgeSlot->SetPadding(FMargin(4,0,4,7));
        Durability=WidgetTree->ConstructWidget<UProgressBar>();Durability->SetVisibility(ESlateVisibility::Collapsed);
        auto* BarBox=WidgetTree->ConstructWidget<USizeBox>();BarBox->SetHeightOverride(3);BarBox->SetContent(Durability);BarBox->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* BarSlot=Decorations->AddChildToOverlay(BarBox);BarSlot->SetHorizontalAlignment(HAlign_Fill);BarSlot->SetVerticalAlignment(VAlign_Bottom);BarSlot->SetPadding(FMargin(4,0,4,2));
        Background->SetPadding(FMargin(5,20,5,24));
    }
    return Super::RebuildWidget();
}
void UAetherInventoryCell::SetItemState(const FAetherV10ItemInstance* I,const FAetherV10ItemDefinition* D,bool Equipped)
{
    if(!Quantity||!Badges||!Durability)return;
    Quantity->SetText(FText::FromString(I?FString::Printf(TEXT("×%d"),I->Quantity):FString()));
    FString State;if(I&&D)
    {
        if(I->bLocked)State+=TEXT("锁 ");if(D->bQuestLocked||I->QuestInstanceId.IsValid())State+=TEXT("任务 ");
        if(I->bFavorite)State+=TEXT("★ ");if(Equipped)State+=TEXT("装备 ");if(I->Quality>0)State+=FString::Printf(TEXT("品质%d"),I->Quality);
    }
    Badges->SetText(FText::FromString(State));const bool HasDurability=I&&D&&D->MaxDurability>0;
    Durability->SetVisibility(HasDurability?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(HasDurability){const float Ratio=FMath::Clamp(float(I->Durability)/D->MaxDurability,0.f,1.f);Durability->SetPercent(Ratio);Durability->SetFillColorAndOpacity(Ratio>.25f?FLinearColor(.2f,.65f,.4f):FLinearColor(.9f,.25f,.15f));}
}
void UAetherInventoryCell::Present(const FAetherInspectRequest& In,int32 SlotValue,const FString& Text,const FString& IconId,bool Filtered,bool Selected)
{
    if(PressedSource.IsSet()&&(Filtered||PressedSlot!=SlotValue||!SameDragSource(PressedSource.GetValue(),In)))PressedSource.Reset();
    const bool NewContext=Request.Context.SessionId!=In.Context.SessionId||Request.Target.InstanceId!=In.Target.InstanceId;
    Request=In;PhysicalSlot=SlotValue;bFiltered=Filtered;SetIsFocusable(!Filtered);TakeWidget();
    ShownText=Filtered?TEXT("筛选外"):Text;Label->SetText(FText::FromString(ShownText+(DropHint.IsEmpty()?FString():LINE_TERMINATOR+DropHint)));
    RestColor=Selected?UAetherUITheme::Get().Accent.CopyWithNewOpacity(.4):UAetherUITheme::Get().Card;
    if(DropHint.IsEmpty())Background->SetBrushColor(RestColor);
    SetRenderOpacity(Filtered?.3f:1.f);
    // 制作管线使用相同有限 IconId 生成图标；资源缺失时保留物品名和格子身份。
    if(ShownIcon!=IconId||NewContext)
    {
        const uint64 Expected=++IconGeneration;
        ShownIcon=IconId;Icon->SetBrushFromTexture(nullptr);Icon->SetVisibility(ESlateVisibility::Collapsed);
        if(!IconId.IsEmpty())
        {
            const FSoftObjectPath Path=AetherWidgetAssets::Icon(IconId);
            const TWeakObjectPtr<UAetherInventoryCell> Self=this;
            UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,[Self,Path,IconId,Expected]()
            {
                if(!Self.IsValid()||Self->ShownIcon!=IconId||Self->IconGeneration!=Expected)return;
                auto* Texture=Cast<UTexture2D>(Path.ResolveObject());Self->Icon->SetBrushFromTexture(Texture);
                Self->Icon->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
            });
        }
    }
}
FReply UAetherInventoryCell::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    PressedSource.Reset();
    if(bFiltered)return FReply::Handled();
    if(E.GetEffectingButton()==EKeys::RightMouseButton){OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::Details);return FReply::Handled();}
    if(E.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        CaptureDragSource(E);const auto Selected=Request;const int32 SelectedSlot=PhysicalSlot;
        // 焦点或选择回调可同步发布新快照；不能在回调后改抓当前格里的替代物品。
        SetUserFocus(GetOwningPlayer());OnIntent.ExecuteIfBound(Selected,SelectedSlot,EAetherCellIntent::Select);
        if(PressedSource.IsSet())return UWidgetBlueprintLibrary::DetectDragIfPressed(E,this,EKeys::LeftMouseButton).NativeReply;
        return FReply::Handled();
    }
    return Super::NativeOnMouseButtonDown(G,E);
}
void UAetherInventoryCell::CaptureDragSource(const FPointerEvent& E)
{
    PressedSource.Reset();
    if(bFiltered||!GetIsEnabled()||!Request.Context.IsValid()||Request.DependencyKey.IsEmpty()||!Request.Target.InstanceId.IsValid()||
        (Request.Target.Kind!=EAetherInspectTarget::ItemInstance&&Request.Target.Kind!=EAetherInspectTarget::EquipmentSlot))return;
    PressedSource=Request;PressedSlot=PhysicalSlot;PressedUser=E.GetUserIndex();PressedPointer=E.GetPointerIndex();bPressedTouch=E.IsTouchEvent();
}
bool UAetherInventoryCell::ConsumeDragSource(const FPointerEvent& E,FAetherInspectRequest& Source)
{
    Source={};const auto Pressed=MoveTemp(PressedSource);PressedSource.Reset();
    if(!Pressed.IsSet()||bFiltered||!GetIsEnabled()||PressedSlot!=PhysicalSlot||PressedUser!=E.GetUserIndex()||
        PressedPointer!=E.GetPointerIndex()||bPressedTouch!=E.IsTouchEvent()||
        (!E.IsTouchEvent()&&!E.IsMouseButtonDown(EKeys::LeftMouseButton))||!SameDragSource(Pressed.GetValue(),Request))return false;
    Source=Pressed.GetValue();return true;
}
FReply UAetherInventoryCell::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{PressedSource.Reset();return Super::NativeOnMouseButtonUp(G,E);}
void UAetherInventoryCell::NativeOnMouseCaptureLost(const FCaptureLostEvent& E)
{PressedSource.Reset();Super::NativeOnMouseCaptureLost(E);}
FReply UAetherInventoryCell::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Gamepad_FaceButton_Left)
    {if(!E.IsRepeat()&&!bFiltered)OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::PickUp);return FReply::Handled();}
    if(E.GetKey()==EKeys::Enter||E.GetKey()==EKeys::Gamepad_FaceButton_Bottom)
    {if(!E.IsRepeat()&&!bFiltered)OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::Place);return FReply::Handled();}
    if(E.GetKey()==EKeys::Gamepad_FaceButton_Top)
    {if(!bFiltered)OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::Details);return FReply::Handled();}
    return Super::NativeOnKeyDown(G,E);
}
void UAetherInventoryCell::NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent)
{
    Super::NativeOnAddedToFocusPath(InFocusEvent);
    if(!bFiltered)OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::Hover);
}
void UAetherInventoryCell::NativeOnMouseEnter(const FGeometry& G,const FPointerEvent& E)
{Super::NativeOnMouseEnter(G,E);if(!bFiltered)OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::Hover);}
void UAetherInventoryCell::NativeOnMouseLeave(const FPointerEvent& E)
{OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::Leave);Super::NativeOnMouseLeave(E);}
void UAetherInventoryCell::NativeOnDragDetected(const FGeometry&,const FPointerEvent& E,UDragDropOperation*& Op)
{
    FAetherInspectRequest Source;if(!ConsumeDragSource(E,Source))return;
    auto* Drag=NewObject<UAetherInventoryDrag>(this);Drag->Source=Source;
    auto* Visual=CreateWidget<UAetherInventoryCell>(GetOwningPlayer(),GetClass());
    Visual->Present(Source,PhysicalSlot,Label->GetText().ToString(),ShownIcon,false,true);
    if(Quantity&&Visual->Quantity)Visual->Quantity->SetText(Quantity->GetText());if(Badges&&Visual->Badges)Visual->Badges->SetText(Badges->GetText());
    if(Durability&&Visual->Durability){Visual->Durability->SetPercent(Durability->GetPercent());Visual->Durability->SetVisibility(Durability->GetVisibility());}
    Visual->SetVisibility(ESlateVisibility::HitTestInvisible);Drag->DefaultDragVisual=Visual;Drag->Pivot=EDragPivot::MouseDown;Op=Drag;
}
void UAetherInventoryCell::NativeOnDragCancelled(const FDragDropEvent& E,UDragDropOperation* Op)
{PressedSource.Reset();Super::NativeOnDragCancelled(E,Op);}
void UAetherInventoryCell::NativeDestruct()
{PressedSource.Reset();Super::NativeDestruct();}
void UAetherInventoryCell::ReleaseSlateResources(bool bReleaseChildren)
{PressedSource.Reset();Super::ReleaseSlateResources(bReleaseChildren);}
bool UAetherInventoryCell::NativeOnDrop(const FGeometry&,const FDragDropEvent&,UDragDropOperation* Op)
{
    const auto* Drag=Cast<UAetherInventoryDrag>(Op);
    const auto Target=DropTarget.IsSet()?DropTarget.GetValue():Request;DropTarget.Reset();
    return !bFiltered&&Drag&&OnItemDrop.IsBound()&&OnItemDrop.Execute(Drag->Source,Target,PhysicalSlot);
}
void UAetherInventoryCell::NativeOnDragEnter(const FGeometry& G,const FDragDropEvent& E,UDragDropOperation* Op)
{
    Super::NativeOnDragEnter(G,E,Op);const auto* Drag=Cast<UAetherInventoryDrag>(Op);if(!Drag)return;
    DropTarget=Request;
    DropHint=TEXT("此处不能放置");const bool Allowed=!bFiltered&&OnDropPreview.IsBound()&&OnDropPreview.Execute(Drag->Source,Request,PhysicalSlot,DropHint);
    Label->SetText(FText::FromString(ShownText+LINE_TERMINATOR+DropHint));Background->SetBrushColor(Allowed?FLinearColor(.08f,.3f,.16f):FLinearColor(.4f,.1f,.08f));
}
void UAetherInventoryCell::NativeOnDragLeave(const FDragDropEvent& E,UDragDropOperation* Op)
{
    DropTarget.Reset();DropHint.Reset();if(Label)Label->SetText(FText::FromString(ShownText));if(Background)Background->SetBrushColor(RestColor);Super::NativeOnDragLeave(E,Op);
}
