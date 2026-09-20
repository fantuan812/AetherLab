#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Presentation/AetherMenuSubsystem.h"
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
class UScrollBox;
class AAetherPlayerState;
UCLASS()
class UAetherFrontierViewModel : public UObject
{
 GENERATED_BODY()
public:
 UPROPERTY(BlueprintReadOnly) FText Heading;
 UPROPERTY(BlueprintReadOnly) FText Body;
 UPROPERTY(BlueprintReadOnly) FText PrimaryLabel;
 UPROPERTY(BlueprintReadOnly) FText SecondaryLabel;
 // 诊断计数用于检查空闲界面没有退回逐帧重建；不参与业务状态或存档。
 uint64 RefreshCount=0;
 void Refresh(AAetherFrontierCharacter* C);
};
UCLASS()
class UAetherFrontierPanel : public UUserWidget
{
 GENERATED_BODY()
public:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeConstruct() override;
 virtual void NativeDestruct() override;
 virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
 // 外部提交/菜单/选择事件驱动快照；仅打开时用低频定时器更新交易距离和队友生命。
 void RefreshSnapshot();
 UWidget* GetPrimaryFocusTarget() const;
private:
 void HandleMenuChanged();
 void BindCharacter();
 void BindProfile();
 void SavePageMemory();
 void RefreshLiveDetails();
 TWeakObjectPtr<UAetherMenuSubsystem> Menu;
 TWeakObjectPtr<AAetherFrontierCharacter> BoundCharacter;
 TWeakObjectPtr<AAetherPlayerState> BoundProfile;
 EAetherMenuPage ShownPage=EAetherMenuPage::None;
 FTimerHandle LiveDetailsTimer;
 UPROPERTY() TObjectPtr<UScrollBox> BodyScroll;
public:
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
 UFUNCTION() void Primary();
 UFUNCTION() void Secondary();
 UFUNCTION() void NextItem();
 UFUNCTION() void ClosePanel();
};
