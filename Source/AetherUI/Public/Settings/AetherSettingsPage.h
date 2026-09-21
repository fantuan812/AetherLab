#pragma once
#include "UI/AetherPageBase.h"
#include "Components/ComboBoxString.h"
#include "Framework/Commands/InputChord.h"
#include "AetherSettingsPage.generated.h"
class USpinBox;
class UInputKeySelector;
class UCheckBox;
class UTextBlock;
class UVerticalBox;
UCLASS()
class AETHERUI_API UAetherSettingsPage : public UAetherPageBase
{
    GENERATED_BODY()
public:
    UAetherSettingsPage(){Page=EAetherMenuPage::System;}
    static void ApplyLocalPreferences(UWorld* World);
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void RefreshPage() override;
    virtual void LiveRefresh() override;
    virtual void PageClosed() override;
    virtual UWidget* InitialFocus() const override;
    bool IsSelectingKey() const;
private:
    void Apply();
    void ConfirmVideo();
    void RevertVideo();
    void SaveDraft();
    void Defaults();
    UFUNCTION() void ActionSelected(FString Value,ESelectInfo::Type Selection);
    UFUNCTION() void KeySelected(FInputChord Chord);
    bool bLoading=false,bVideoPending=false;
    double ConfirmDeadline=0;
    FIntPoint BeforeResolution;
    int32 BeforeWindowMode=0,BeforeQuality=0;
    bool BeforeVsync=false;
    TMap<FName,FKey> DraftKeys;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UComboBoxString> Resolution;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UComboBoxString> WindowMode;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UComboBoxString> Quality;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UComboBoxString> Backend;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UComboBoxString> BindingAction;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UInputKeySelector> BindingKey;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USpinBox> Volume;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USpinBox> Mouse;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USpinBox> Controller;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USpinBox> Scale;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UCheckBox> Invert;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UCheckBox> Vsync;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Notice;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Provider;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UVerticalBox> Confirmation;
};
