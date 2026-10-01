#pragma once
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "Inspection/AetherInspectionService.h"
#include "AetherInventoryCell.generated.h"
class UBorder;
class UTextBlock;
class UImage;
class UOverlay;
class UProgressBar;
enum class EAetherCellIntent:uint8 {Hover,Select,Details,Leave,PickUp,Place};
DECLARE_DELEGATE_ThreeParams(FOnAetherCellIntent,const FAetherInspectRequest&,int32,EAetherCellIntent);
DECLARE_DELEGATE_RetVal_ThreeParams(bool,FOnAetherCellDrop,const FAetherInspectRequest&,const FAetherInspectRequest&,int32);
DECLARE_DELEGATE_RetVal_FourParams(bool,FOnAetherDropPreview,const FAetherInspectRequest&,const FAetherInspectRequest&,int32,FString&);

UCLASS()
class AETHERUI_API UAetherInventoryDrag : public UDragDropOperation
{
    GENERATED_BODY()
public:
    FAetherInspectRequest Source;
};

UCLASS()
class AETHERUI_API UAetherInventoryCell : public UUserWidget
{
    GENERATED_BODY()
public:
    void Present(const FAetherInspectRequest& Request,int32 PhysicalSlot,const FString& Label,const FString& IconId,bool Filtered,bool Selected);
    FOnAetherCellIntent OnIntent;
    FOnAetherCellDrop OnItemDrop;
    FOnAetherDropPreview OnDropPreview;
    void SetItemState(const FAetherV10ItemInstance* Item,const FAetherV10ItemDefinition* Definition,bool Equipped=false);
    bool IsFiltered() const{return bFiltered;}
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent) override;
    virtual void NativeOnMouseEnter(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeOnMouseLeave(const FPointerEvent& Event) override;
    virtual void NativeOnDragDetected(const FGeometry& Geometry,const FPointerEvent& Event,UDragDropOperation*& Operation) override;
    virtual void NativeOnDragCancelled(const FDragDropEvent& Event,UDragDropOperation* Operation) override;
    virtual void NativeDestruct() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    virtual bool NativeOnDrop(const FGeometry& Geometry,const FDragDropEvent& Event,UDragDropOperation* Operation) override;
    virtual void NativeOnDragEnter(const FGeometry& Geometry,const FDragDropEvent& Event,UDragDropOperation* Operation) override;
    virtual void NativeOnDragLeave(const FDragDropEvent& Event,UDragDropOperation* Operation) override;
private:
    friend class FAetherInventoryDragLifecycleTest;
    void CaptureDragSource(const FPointerEvent& Event);
    bool ConsumeDragSource(const FPointerEvent& Event,FAetherInspectRequest& Source);
    FAetherInspectRequest Request;
    // 仅保存尚未越过拖动阈值的按下意图；已发命令仍完全由 CommandClient 保管。
    TOptional<FAetherInspectRequest> PressedSource;
    int32 PressedSlot=INDEX_NONE;
    uint32 PressedUser=0,PressedPointer=0;
    bool bPressedTouch=false;
    TOptional<FAetherInspectRequest> DropTarget;
    int32 PhysicalSlot=INDEX_NONE;
    bool bFiltered=false;
    FString ShownIcon;
    FString ShownText,DropHint;
    FLinearColor RestColor;
    uint64 IconGeneration=0;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UBorder> Background;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Label;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> Icon;
    UPROPERTY(Transient) TObjectPtr<UOverlay> Decorations;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Quantity;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Badges;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> Durability;
};
