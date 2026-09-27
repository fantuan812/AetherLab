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
enum class EAetherCellIntent:uint8 {Hover,Select,Details,Leave};
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
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void NativeOnMouseEnter(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeOnMouseLeave(const FPointerEvent& Event) override;
    virtual void NativeOnDragDetected(const FGeometry& Geometry,const FPointerEvent& Event,UDragDropOperation*& Operation) override;
    virtual bool NativeOnDrop(const FGeometry& Geometry,const FDragDropEvent& Event,UDragDropOperation* Operation) override;
    virtual void NativeOnDragEnter(const FGeometry& Geometry,const FDragDropEvent& Event,UDragDropOperation* Operation) override;
    virtual void NativeOnDragLeave(const FDragDropEvent& Event,UDragDropOperation* Operation) override;
private:
    FAetherInspectRequest Request;
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
