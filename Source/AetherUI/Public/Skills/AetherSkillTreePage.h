#pragma once
#include "Blueprint/UserWidget.h"
#include "Inspection/AetherInspectionSession.h"
#include "Skills/AetherSkillTreeModel.h"
#include "AetherSkillTreePage.generated.h"

class AAetherFrontierCharacter;
class UAetherMenuSubsystem;
class UAetherSkillGraphWidget;
class UAetherInspectionCard;
class UAetherInspectionConfirmation;
class UHorizontalBox;
class UTextBlock;
class UBorder;
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAetherSkillCommandReady,const FAetherInspectionDispatch&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAetherSkillQuestRequested,const FString&);

// 图形技能页只消费已发布的快照。生产命令服务接入时订阅 OnCommandReady，并交回真实回执与提交快照。
UCLASS()
class AETHERUI_API UAetherSkillTreePage : public UUserWidget
{
    GENERATED_BODY()
public:
    void PublishSnapshot(const FAetherInspectionSnapshot& Snapshot);
    void ReceiveReceipt(const FAetherCommandResult& Result);
    void SetLegacySource(AAetherFrontierCharacter* Character);
    void ClosePresentation();
    UWidget* GetNavigationFocusTarget() const;
    FOnAetherSkillCommandReady OnCommandReady;
    FOnAetherSkillQuestRequested OnTrackQuest;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeDestruct() override;
private:
    void Refresh();
    void Select(const FAetherSkillNodeIdentity& Node,bool FocusDetail);
    void RequestAction(const FAetherInspectRequest& Request,const FAetherInspectionAction& Action);
    void Confirm(FGuid Token,int32 Quantity);
    void Cancel(FGuid Token);
    void ShowConfirmation();
    void HideConfirmation();
    void HandleMenu();
    void ApplyCommandAvailability(FAetherInspectionModel& Model) const;
    void SelectHotbar(const FAetherInspectRequest& Request,const FAetherInspectionAction& Action,bool Comparison);
    UFUNCTION() void PreviousSources();
    UFUNCTION() void NextSources();
    UFUNCTION() void ResetGraphView();
    void RenderSources();
    FAetherInspectionSnapshot Snapshot;
    FAetherInspectionSession Session;
    FAetherSkillTreeModel TreeModel;
    TOptional<FAetherSkillNodeIdentity> Selected,PendingNode;
    TWeakObjectPtr<AAetherFrontierCharacter> LegacySource,NativePawn;
    TWeakObjectPtr<UAetherMenuSubsystem> Menu;
    bool bNativeSnapshot=false;
    int32 LegacyRevision=-1,SourcePage=0;
    FGuid ModalToken;
    UPROPERTY(Transient) TObjectPtr<UAetherSkillGraphWidget> Graph;
    UPROPERTY(Transient) TObjectPtr<UAetherInspectionCard> Details;
    UPROPERTY(Transient) TObjectPtr<UAetherInspectionConfirmation> Confirmation;
    UPROPERTY(Transient) TObjectPtr<UBorder> ModalShield;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Points;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Notice;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Sources;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> Hotbar;
};
