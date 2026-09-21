#pragma once
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "Inspection/AetherInspectionService.h"
#include "AetherInventoryCell.generated.h"
class UBorder;
class UTextBlock;
class UImage;
enum class EAetherCellIntent:uint8 {Hover,Select,Details,Leave};
DECLARE_DELEGATE_ThreeParams(FOnAetherCellIntent,const FAetherInspectRequest&,int32,EAetherCellIntent);
DECLARE_DELEGATE_RetVal_ThreeParams(bool,FOnAetherCellDrop,const FAetherInspectRequest&,const FAetherInspectRequest&,int32);

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
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void NativeOnMouseEnter(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeOnMouseLeave(const FPointerEvent& Event) override;
    virtual void NativeOnDragDetected(const FGeometry& Geometry,const FPointerEvent& Event,UDragDropOperation*& Operation) override;
    virtual bool NativeOnDrop(const FGeometry& Geometry,const FDragDropEvent& Event,UDragDropOperation* Operation) override;
private:
    FAetherInspectRequest Request;
    int32 PhysicalSlot=INDEX_NONE;
    bool bFiltered=false;
    FString ShownIcon;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UBorder> Background;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Label;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UImage> Icon;
};
