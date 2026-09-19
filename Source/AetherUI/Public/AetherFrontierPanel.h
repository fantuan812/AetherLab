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
class USpinBox;
class UButton;
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
 UPROPERTY() TObjectPtr<UHorizontalBox> InventoryRow;
 UPROPERTY() TObjectPtr<USpinBox> Quantity;
 UPROPERTY() TObjectPtr<UComboBoxString> MergeTarget;
 UPROPERTY() TObjectPtr<UComboBoxString> Product;
 UPROPERTY() TObjectPtr<UHorizontalBox> TradeRow;
 UPROPERTY() TObjectPtr<UTextBlock> TradeInfo;
 UPROPERTY() TObjectPtr<UButton> BuyButton;
 UPROPERTY() TObjectPtr<UButton> RetryButton;
 UPROPERTY() TObjectPtr<UButton> SellButton;
 UPROPERTY() TObjectPtr<UTextBlock> SellText;
 FName ShownShop;
 FGuid ShownTradeToken;
 void RefreshTrade(AAetherFrontierCharacter* Character,bool Open);
 TArray<FGuid> MergeInstances;
 int32 ShownRevision=-1;
 UFUNCTION() void SetQuantity(float Value);
 UFUNCTION() void SetMergeTarget(FString Value,ESelectInfo::Type Selection);
 UFUNCTION() void Split();
 UFUNCTION() void Merge();
 UFUNCTION() void Buy();
 UFUNCTION() void RetryPending();
 UFUNCTION() void Sell();
 bool UpdatingKey=false;
 UFUNCTION() void ActionSelected(FString Action,ESelectInfo::Type Selection);
 UFUNCTION() void KeySelected(FInputChord Key);
 bool WasOpen=false;
 UFUNCTION() void Primary();
 UFUNCTION() void Secondary();
 UFUNCTION() void NextItem();
 UFUNCTION() void ClosePanel();
};
