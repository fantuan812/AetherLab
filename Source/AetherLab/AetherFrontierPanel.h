#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Commands/InputChord.h"
#include "Components/ComboBoxString.h"
#include "AetherFrontierPanel.generated.h"
class AAetherFrontierCharacter;
class UTextBlock;
class UComboBoxString;
class UInputKeySelector;
class UHorizontalBox;
UCLASS()
class UAetherFrontierViewModel : public UObject
{
 GENERATED_BODY()
public:
 UPROPERTY(BlueprintReadOnly) FText Heading;
 UPROPERTY(BlueprintReadOnly) FText Body;
 UPROPERTY(BlueprintReadOnly) FText PrimaryLabel;
 UPROPERTY(BlueprintReadOnly) FText SecondaryLabel;
 void Refresh(AAetherFrontierCharacter* C);
};
UCLASS()
class UAetherFrontierPanel : public UUserWidget
{
 GENERATED_BODY()
public:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeConstruct() override;
 virtual void NativeTick(const FGeometry& Geometry,float Dt) override;
 UPROPERTY() TObjectPtr<UAetherFrontierViewModel> Model;
private:
 UPROPERTY() TObjectPtr<UTextBlock> Heading;
 UPROPERTY() TObjectPtr<UTextBlock> Body;
 UPROPERTY() TObjectPtr<UTextBlock> PrimaryText;
 UPROPERTY() TObjectPtr<UTextBlock> SecondaryText;
 UPROPERTY() TObjectPtr<UHorizontalBox> SettingsRow;
 UPROPERTY() TObjectPtr<UComboBoxString> BindingAction;
 UPROPERTY() TObjectPtr<UInputKeySelector> BindingKey;
 bool UpdatingKey=false;
 UFUNCTION() void ActionSelected(FString Action,ESelectInfo::Type Selection);
 UFUNCTION() void KeySelected(FInputChord Key);
 bool WasOpen=false;
 UFUNCTION() void Primary();
 UFUNCTION() void Secondary();
 UFUNCTION() void NextItem();
 UFUNCTION() void ClosePanel();
};
