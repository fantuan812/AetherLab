#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AetherCharacterPreviewWidget.generated.h"
class AAetherCharacter;
class UAetherCharacterPreviewSubsystem;
class UImage;
class UTextBlock;

// 小型可组合控件，后续 WBP_CharacterPreview 可直接嵌入；不持有任何真实玩法组件。
UCLASS()
class AETHERUI_API UAetherCharacterPreviewWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetSource(AAetherCharacter* Character);
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event) override;
private:
    void RefreshDisplay();
    UFUNCTION() void ResetCamera();
    UFUNCTION() void TurnLeft();
    UFUNCTION() void TurnRight();
    UFUNCTION() void ZoomIn();
    UFUNCTION() void ZoomOut();
    TWeakObjectPtr<UAetherCharacterPreviewSubsystem> Preview;
    TWeakObjectPtr<AAetherCharacter> RequestedSource;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> Image;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Caption;
    bool bDragging=false;
};
