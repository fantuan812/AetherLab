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
    UPROPERTY() TObjectPtr<UComboBoxString> Resolution;
    UPROPERTY() TObjectPtr<UComboBoxString> WindowMode;
    UPROPERTY() TObjectPtr<UComboBoxString> Quality;
    UPROPERTY() TObjectPtr<UComboBoxString> Backend;
    UPROPERTY() TObjectPtr<UComboBoxString> BindingAction;
    UPROPERTY() TObjectPtr<UInputKeySelector> BindingKey;
    UPROPERTY() TObjectPtr<USpinBox> Volume;
    UPROPERTY() TObjectPtr<USpinBox> Mouse;
    UPROPERTY() TObjectPtr<USpinBox> Controller;
    UPROPERTY() TObjectPtr<USpinBox> Scale;
    UPROPERTY() TObjectPtr<UCheckBox> Invert;
    UPROPERTY() TObjectPtr<UCheckBox> Vsync;
    UPROPERTY() TObjectPtr<UTextBlock> Notice;
    UPROPERTY() TObjectPtr<UTextBlock> Provider;
    UPROPERTY() TObjectPtr<UVerticalBox> Confirmation;
};
