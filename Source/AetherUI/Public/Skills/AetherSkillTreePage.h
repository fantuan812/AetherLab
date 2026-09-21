#pragma once
#include "Blueprint/UserWidget.h"
#include "Inspection/AetherInspectionSession.h"
#include "Skills/AetherSkillTreeModel.h"
#include "AetherSkillTreePage.generated.h"

class AAetherFrontierCharacter;
class UAetherCommandClient;
class UAetherMenuSubsystem;
class UAetherSkillGraphWidget;
class UAetherInspectionCard;
class UAetherInspectionConfirmation;
class UHorizontalBox;
class UTextBlock;
class UBorder;
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAetherSkillCommandReady,const FAetherInspectionDispatch&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAetherSkillQuestRequested,const FString&);

// 图形技能页消费 LocalPlayer 的已发布快照；原生后端未安装时保留旧档案只读投影。
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
    void HandleNativeProfile();
    void DispatchNativeCommand(const FAetherInspectionDispatch& Dispatch);
    UFUNCTION() void RetryNativeCommand();
    void ApplyCommandAvailability(FAetherInspectionModel& Model) const;
    void DropSkill(const FAetherInspectRequest& Request,int32 Slot);
    void InspectHotbar(const FAetherInspectRequest& Request);
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
    TWeakObjectPtr<UAetherCommandClient> CommandClient;
    bool bNativeSnapshot=false;
    FString NativeSnapshotKey;
    int64 ViewGeneration=0;
    FTimerHandle ViewTimer;
    int32 LegacyRevision=-1,SourcePage=0;
    FGuid ModalToken;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UAetherSkillGraphWidget> Graph;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UAetherInspectionCard> Details;
    UPROPERTY(Transient) TObjectPtr<UAetherInspectionConfirmation> Confirmation;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UBorder> ModalShield;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Points;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Notice;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Sources;
    UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UHorizontalBox> Hotbar;
};
