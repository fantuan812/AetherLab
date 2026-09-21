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
#include "Engine/Texture2D.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "InputCoreTypes.h"

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
    return Super::RebuildWidget();
}
void UAetherInventoryCell::Present(const FAetherInspectRequest& In,int32 SlotValue,const FString& Text,const FString& IconId,bool Filtered,bool Selected)
{
    Request=In;PhysicalSlot=SlotValue;bFiltered=Filtered;TakeWidget();
    Label->SetText(FText::FromString(Filtered?TEXT("筛选外"):Text));
    Background->SetBrushColor(Selected?UAetherUITheme::Get().Accent.CopyWithNewOpacity(.4):UAetherUITheme::Get().Card);
    SetRenderOpacity(Filtered?.3f:1.f);
    // 制作管线使用相同有限 IconId 生成图标；资源缺失时保留物品名和格子身份。
    if(ShownIcon!=IconId)
    {
        ShownIcon=IconId;Icon->SetBrushFromTexture(nullptr);Icon->SetVisibility(ESlateVisibility::Collapsed);
        if(!IconId.IsEmpty())
        {
            const FSoftObjectPath Path=AetherWidgetAssets::Icon(IconId);
            const TWeakObjectPtr<UAetherInventoryCell> Self=this;
            UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,[Self,Path,IconId]()
            {
                if(!Self.IsValid()||Self->ShownIcon!=IconId)return;
                auto* Texture=Cast<UTexture2D>(Path.ResolveObject());Self->Icon->SetBrushFromTexture(Texture);
                Self->Icon->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
            });
        }
    }
}
FReply UAetherInventoryCell::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    if(bFiltered)return FReply::Handled();
    if(E.GetEffectingButton()==EKeys::RightMouseButton){OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::Details);return FReply::Handled();}
    if(E.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        SetUserFocus(GetOwningPlayer());OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::Select);
        if(Request.Target.InstanceId.IsValid())return UWidgetBlueprintLibrary::DetectDragIfPressed(E,this,EKeys::LeftMouseButton).NativeReply;
        return FReply::Handled();
    }
    return Super::NativeOnMouseButtonDown(G,E);
}
FReply UAetherInventoryCell::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Enter||E.GetKey()==EKeys::Gamepad_FaceButton_Bottom||E.GetKey()==EKeys::Gamepad_FaceButton_Top)
    {if(!bFiltered)OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::Details);return FReply::Handled();}
    return Super::NativeOnKeyDown(G,E);
}
void UAetherInventoryCell::NativeOnMouseEnter(const FGeometry& G,const FPointerEvent& E)
{Super::NativeOnMouseEnter(G,E);if(!bFiltered)OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::Hover);}
void UAetherInventoryCell::NativeOnMouseLeave(const FPointerEvent& E)
{OnIntent.ExecuteIfBound(Request,PhysicalSlot,EAetherCellIntent::Leave);Super::NativeOnMouseLeave(E);}
void UAetherInventoryCell::NativeOnDragDetected(const FGeometry&,const FPointerEvent&,UDragDropOperation*& Op)
{
    if(bFiltered||!Request.Target.InstanceId.IsValid())return;
    auto* Drag=NewObject<UAetherInventoryDrag>(this);Drag->Source=Request;Op=Drag;
}
bool UAetherInventoryCell::NativeOnDrop(const FGeometry&,const FDragDropEvent&,UDragDropOperation* Op)
{
    const auto* Drag=Cast<UAetherInventoryDrag>(Op);
    return !bFiltered&&Drag&&OnItemDrop.IsBound()&&OnItemDrop.Execute(Drag->Source,Request,PhysicalSlot);
}
