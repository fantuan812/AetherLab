#pragma once
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "Inventory/AetherInventoryCell.h"
#include "Inspection/AetherInspectionSession.h"
#include "Contracts/AetherPlayerCommand.h"
#include "AetherInventoryPage.generated.h"
class UAetherCommandClient;
class UAetherMenuSubsystem;
class AAetherFrontierCharacter;
class UAetherCharacterPreviewWidget;
class UAetherInspectionCard;
class UAetherInspectionConfirmation;
class UUniformGridPanel;
class UVerticalBox;
class UTextBlock;
class UEditableTextBox;
class UComboBoxString;
class UBorder;

UCLASS()
class AETHERUI_API UAetherInventoryPage : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    void Refresh();
    void ClosePresentation();
    UWidget* GetNavigationFocusTarget() const;
private:
    void MenuChanged();
    void Receive(const FAetherCommandResult& Result);
    void CellIntent(const FAetherInspectRequest& Request,int32 Slot,EAetherCellIntent Intent);
    bool Drop(const FAetherInspectRequest& From,const FAetherInspectRequest& To,int32 Slot);
    void Action(const FAetherInspectRequest& Request,const FAetherInspectionAction& Action);
    void Compare(const FAetherInspectRequest& Request,const FString& Slot);
    void Confirm(FGuid Token,int32 Quantity);
    void Cancel(FGuid Token);
    void HideConfirmation();
    void RenderDetails();
    void DismissDetails();
    bool Send(FAetherPlayerCommand Command);
    UFUNCTION() void SearchChanged(const FText& Text);
    UFUNCTION() void CategoryChanged(FString Category,ESelectInfo::Type Selection);
    UFUNCTION() void Sort();
    UFUNCTION() void Retry();
    FAetherInspectionSnapshot Snapshot;
    FAetherInspectionSession Session;
    TWeakObjectPtr<UAetherCommandClient> Client;
    TWeakObjectPtr<UAetherMenuSubsystem> Menu;
    TWeakObjectPtr<AAetherFrontierCharacter> Player;
    int64 Generation=0,SeenProfile=-1,SeenWorld=-1;
    FGuid SeenChannel,SeenTrade,ModalToken;
    FString CategoryFilter,SearchFilter,SeenShop;
    FGuid SeenSelected;
    bool bUpdating=false,bWasOpen=false,bSeenCanAct=false,bDirty=true;
    FTimerHandle ServiceTimer;
    UPROPERTY(Transient) TObjectPtr<UUniformGridPanel> Grid;
    UPROPERTY(Transient) TObjectPtr<UUniformGridPanel> Equipment;
    UPROPERTY(Transient) TObjectPtr<UUniformGridPanel> Products;
    UPROPERTY(Transient) TArray<TObjectPtr<UAetherInventoryCell>> Cells;
    UPROPERTY(Transient) TArray<TObjectPtr<UAetherInventoryCell>> EquipmentCells;
    UPROPERTY(Transient) TArray<TObjectPtr<UAetherInventoryCell>> ProductCells;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Summary;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Notice;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Search;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> Categories;
    UPROPERTY(Transient) TObjectPtr<UAetherCharacterPreviewWidget> Preview;
    UPROPERTY(Transient) TObjectPtr<UAetherInspectionCard> Details;
    UPROPERTY(Transient) TObjectPtr<UAetherInspectionCard> Hover;
    UPROPERTY(Transient) TObjectPtr<UAetherInspectionConfirmation> Confirmation;
    UPROPERTY(Transient) TObjectPtr<UBorder> ModalShield;
};
