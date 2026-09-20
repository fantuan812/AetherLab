#include "Skills/AetherSkillTreePage.h"
#include "Skills/AetherSkillGraphWidget.h"
#include "Inspection/AetherInspectionWidgets.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Characters/AetherFrontierCharacter.h"
#include "AetherFrontier.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "InputCoreTypes.h"

TSharedRef<SWidget> UAetherSkillTreePage::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
    if(!WidgetTree->RootWidget)
    {
        auto* Overlay=WidgetTree->ConstructWidget<UOverlay>();WidgetTree->RootWidget=Overlay;
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();Overlay->AddChildToOverlay(Root);
        auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Header);
        Points=WidgetTree->ConstructWidget<UTextBlock>();Header->AddChildToHorizontalBox(Points)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Reset=WidgetTree->ConstructWidget<UButton>();auto* ResetText=WidgetTree->ConstructWidget<UTextBlock>();
        ResetText->SetText(FText::FromString(TEXT("重置视图")));Reset->SetContent(ResetText);Header->AddChildToHorizontalBox(Reset);
        Reset->OnClicked.AddDynamic(this,&UAetherSkillTreePage::ResetGraphView);
        Notice=WidgetTree->ConstructWidget<UTextBlock>();Notice->SetAutoWrapText(true);Root->AddChildToVerticalBox(Notice);
        auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Graph=WidgetTree->ConstructWidget<UAetherSkillGraphWidget>();Graph->OnNodeSelected.AddUObject(this,&UAetherSkillTreePage::Select);
        auto* GraphSlot=Body->AddChildToHorizontalBox(Graph);FSlateChildSize GraphSize(ESlateSizeRule::Fill);GraphSize.Value=.62f;GraphSlot->SetSize(GraphSize);
        Details=CreateWidget<UAetherInspectionCard>(this,UAetherInspectionCard::StaticClass());
        Details->OnActionRequested.AddUObject(this,&UAetherSkillTreePage::RequestAction);
        Details->OnDismissRequested.AddUObject(this,&UAetherSkillTreePage::ClosePresentation);
        auto* DetailsSlot=Body->AddChildToHorizontalBox(Details);FSlateChildSize DetailSize(ESlateSizeRule::Fill);DetailSize.Value=.38f;DetailsSlot->SetSize(DetailSize);DetailsSlot->SetPadding(FMargin(12,0,0,0));
        Hotbar=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Hotbar)->SetPadding(FMargin(0,8));
        Sources=WidgetTree->ConstructWidget<UTextBlock>();Sources->SetAutoWrapText(true);Root->AddChildToVerticalBox(Sources);
        auto* Pages=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Pages);
        auto PageButton=[&](const TCHAR* Label){auto* B=WidgetTree->ConstructWidget<UButton>();auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(Label));B->SetContent(T);Pages->AddChildToHorizontalBox(B);return B;};
        PageButton(TEXT("点数来源上一页"))->OnClicked.AddDynamic(this,&UAetherSkillTreePage::PreviousSources);
        PageButton(TEXT("点数来源下一页"))->OnClicked.AddDynamic(this,&UAetherSkillTreePage::NextSources);
        // 遮罩占满本页，数量确认保持独立焦点，点击不会透到下面的节点或快捷位。
        ModalShield=WidgetTree->ConstructWidget<UBorder>();ModalShield->SetBrushColor(FLinearColor(0,0,0,.75f));
        auto* ModalSlot=Overlay->AddChildToOverlay(ModalShield);ModalSlot->SetHorizontalAlignment(HAlign_Fill);ModalSlot->SetVerticalAlignment(VAlign_Fill);
        ModalShield->SetHorizontalAlignment(HAlign_Center);ModalShield->SetVerticalAlignment(VAlign_Center);
        Confirmation=CreateWidget<UAetherInspectionConfirmation>(this,UAetherInspectionConfirmation::StaticClass());ModalShield->SetContent(Confirmation);
        Confirmation->OnConfirmed.AddUObject(this,&UAetherSkillTreePage::Confirm);Confirmation->OnCancelled.AddUObject(this,&UAetherSkillTreePage::Cancel);
        ModalShield->SetVisibility(ESlateVisibility::Collapsed);
    }
    Refresh();return Super::RebuildWidget();
}
void UAetherSkillTreePage::NativeConstruct()
{
    Super::NativeConstruct();
    if(auto* LP=GetOwningLocalPlayer())Menu=LP->GetSubsystem<UAetherMenuSubsystem>();
    if(Menu.IsValid()){Menu->OnChanged.RemoveAll(this);Menu->OnChanged.AddUObject(this,&UAetherSkillTreePage::HandleMenu);}
}
void UAetherSkillTreePage::NativeDestruct()
{
    if(Menu.IsValid())Menu->OnChanged.RemoveAll(this);
    ClosePresentation();Menu.Reset();OnCommandReady.Clear();OnTrackQuest.Clear();Super::NativeDestruct();
}
void UAetherSkillTreePage::PublishSnapshot(const FAetherInspectionSnapshot& S)
{
    const bool NewOwner=!Snapshot.Context.OwnerIdentity.Equals(S.Context.OwnerIdentity,ESearchCase::CaseSensitive)||Snapshot.Context.SessionId!=S.Context.SessionId;
    if(NewOwner){HideConfirmation();Session=FAetherInspectionSession();Selected.Reset();SourcePage=0;}
    bNativeSnapshot=true;NativePawn=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());Snapshot=S;LegacySource.Reset();LegacyRevision=-1;
    Session.Refresh(Snapshot,FAetherV10ItemDefinitions(),FAetherSkillDefinitionsV10::Get());
    if(!Session.GetDraft().IsSet())HideConfirmation();
    Refresh();
}
void UAetherSkillTreePage::ReceiveReceipt(const FAetherCommandResult& Result)
{
    if(Session.Acknowledge(Result))
    {Session.Refresh(Snapshot,FAetherV10ItemDefinitions(),FAetherSkillDefinitionsV10::Get());Refresh();}
}
void UAetherSkillTreePage::SetLegacySource(AAetherFrontierCharacter* C)
{
    if(bNativeSnapshot)return;
    if(!C||!C->ProfileState()){ClosePresentation();LegacySource.Reset();LegacyRevision=-1;Snapshot={};Refresh();return;}
    const auto& P=C->ProfileState()->Profile;
    const bool NewOwner=LegacySource.Get()!=C||!Snapshot.Context.OwnerIdentity.Equals(P.CharacterId,ESearchCase::CaseSensitive);
    if(!NewOwner&&LegacyRevision==P.Revision)return;
    if(NewOwner){HideConfirmation();Session=FAetherInspectionSession();Selected.Reset();Snapshot={};Snapshot.Context.SessionId=FGuid::NewGuid();SourcePage=0;}
    LegacySource=C;LegacyRevision=P.Revision;Snapshot.Context.OwnerIdentity=P.CharacterId;Snapshot.Context.SnapshotRevision=P.Revision;Snapshot.ProfileRevision=P.Revision;
    FString Reason;
    // 冻结 v9 只能证明故事基础已学，不能凭空推断技能点、购买账本或退款；此路径明确只读。
    if(!FAetherSkillStateV10::FromLegacyMask(P.LearnedSpells,FAetherSkillDefinitionsV10::Get(),Snapshot.Skills,Reason))
    {Snapshot.Context={};Refresh();return;}
    Snapshot.SkillContext.CharacterLevel=FMath::Clamp(1+P.Experience/200,1,100);
    Snapshot.SkillContext.CompletedQuests.Reset();for(FName Claim:P.Claims)Snapshot.SkillContext.CompletedQuests.Add(Claim.ToString());
    Session.Refresh(Snapshot,FAetherV10ItemDefinitions(),FAetherSkillDefinitionsV10::Get());Refresh();
}
void UAetherSkillTreePage::ApplyCommandAvailability(FAetherInspectionModel& M) const
{
    if(bNativeSnapshot&&OnCommandReady.IsBound())return;
    for(auto& A:M.Actions)if(A.Kind!=EAetherInspectAction::FocusSkill&&A.Kind!=EAetherInspectAction::TrackQuest)
    {A.bEnabled=false;A.DisabledReason=TEXT("技能成长暂不可用；基础能力仍可向导师学习。");}
}
void UAetherSkillTreePage::Refresh()
{
    if(!Session.GetPendingDispatch().IsSet())PendingNode.Reset();
    TreeModel=AetherSkillTree::Build(Snapshot,FAetherSkillDefinitionsV10::Get(),FAetherSkillTreeLayout::Get(),PendingNode);
    if(!Graph||!Points||!Details)return;
    Graph->SetModel(TreeModel);
    Points->SetText(FText::FromString(FString::Printf(TEXT("技能点：%d"),TreeModel.AvailablePoints)));
    Notice->SetText(FText::FromString(!TreeModel.bValid?TreeModel.Message:
        bNativeSnapshot?TEXT("点击查看 · 右键详情 · 空白拖动 / 滚轮缩放 · 方向键或手柄方向键选择"):
        TEXT("当前展示已学基础能力和成长路线。技能成长暂不可用；基础能力仍可向导师学习。")));
    if(Session.GetDetails().IsSet()){auto M=Session.GetDetails().GetValue();ApplyCommandAvailability(M);Details->SetModel(M);}
    else if(TreeModel.bValid&&!TreeModel.Nodes.IsEmpty())Select(Selected.IsSet()?Selected.GetValue():TreeModel.Nodes[0].Identity,false);
    else Details->SetModel(FAetherInspectionModel());
    Hotbar->ClearChildren();
    for(const auto& H:TreeModel.Hotbar)
    {
        auto* B=WidgetTree->ConstructWidget<UAetherInspectionActionButton>();
        FAetherInspectTarget T;T.Kind=EAetherInspectTarget::SkillNode;T.DefinitionId=H.SkillId;T.SkillRank=FMath::Max(1,H.EffectiveRank);
        FAetherInspectionAction A;A.Kind=EAetherInspectAction::FocusSkill;A.Argument=H.SkillId;A.bEnabled=!H.SkillId.IsEmpty();
        B->InitializeAction(AetherInspection::Pin(Snapshot,T),A);B->OnRequested.AddUObject(this,&UAetherSkillTreePage::SelectHotbar);
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetAutoWrapText(true);
        Label->SetText(FText::FromString(FString::Printf(TEXT("%d · %s%s"),H.Slot+1,H.Title.IsEmpty()?TEXT("空位"):*H.Title,H.SkillId.IsEmpty()?TEXT(""):H.bAvailable?TEXT(" · 可用"):TEXT(" · 未授权"))));
        B->SetContent(Label);Hotbar->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
    RenderSources();
}
void UAetherSkillTreePage::Select(const FAetherSkillNodeIdentity& Node,bool FocusDetail)
{
    if(!TreeModel.Nodes.ContainsByPredicate([&](const auto& N){return N.Identity==Node;}))return;
    HideConfirmation();Selected=Node;FAetherInspectTarget T;T.Kind=EAetherInspectTarget::SkillNode;T.DefinitionId=Node.SkillId;T.SkillRank=Node.Rank;
    Session.OpenDetails(T,Snapshot,FAetherV10ItemDefinitions(),FAetherSkillDefinitionsV10::Get());
    if(Details){auto M=Session.GetDetails().GetValue();ApplyCommandAvailability(M);Details->SetModel(M);if(FocusDetail)Details->SetKeyboardFocus();}
    if(Graph)Graph->SelectNode(Node);
}
void UAetherSkillTreePage::RequestAction(const FAetherInspectRequest& R,const FAetherInspectionAction& A)
{
    if(!R.Context.Same(Snapshot.Context)||!Session.GetDetails().IsSet()||
        !R.Target.DefinitionId.Equals(Session.GetDetails()->Request.Target.DefinitionId,ESearchCase::CaseSensitive)||
        R.Target.SkillRank!=Session.GetDetails()->Request.Target.SkillRank)return;
    const bool Navigation=A.Kind==EAetherInspectAction::FocusSkill||A.Kind==EAetherInspectAction::TrackQuest;
    if(!Navigation&&(!bNativeSnapshot||!OnCommandReady.IsBound()))return;
    const FGuid Token=Session.BeginAction(A.Kind,A.Argument);if(!Token.IsValid())return;
    if(A.bNeedsConfirmation||A.MaxQuantity>1)ShowConfirmation();else Confirm(Token,1);
}
void UAetherSkillTreePage::Confirm(FGuid Token,int32 Count)
{
    // 旧确认回调不能关闭后来打开的新弹窗；先检查令牌再执行或拆除任何控件。
    if(!Session.GetDraft().IsSet()||Session.GetDraft()->Token!=Token)return;
    const auto Kind=Session.GetDraft()->Action.Kind;
    if(Kind!=EAetherInspectAction::FocusSkill&&Kind!=EAetherInspectAction::TrackQuest&&(!bNativeSnapshot||!OnCommandReady.IsBound()))
    {Session.Back();HideConfirmation();Refresh();return;}
    FAetherInspectionDispatch Dispatch;FString Reason;
    const bool Done=Session.Confirm(Token,Count,Snapshot,FAetherV10ItemDefinitions(),FAetherSkillDefinitionsV10::Get(),Dispatch,Reason);
    HideConfirmation();
    if(Done)
    {
        if(Dispatch.Command.IsSet())
        {
            const auto& Command=Dispatch.Command.GetValue();
            if(Command.Type==EAetherCommandType::LearnSkill||Command.Type==EAetherCommandType::UpgradeSkill)
                PendingNode=FAetherSkillNodeIdentity{Command.SkillId,Snapshot.Skills.PermanentRank(Command.SkillId)+1};
            // 先标记固定目标，再通知可能同步回调快照/回执的命令服务。
            OnCommandReady.Broadcast(Dispatch);
        }
        else if(Dispatch.Action==EAetherInspectAction::TrackQuest)
        {
            if(auto* C=LegacySource.Get())C->TrackedQuest=FName(*Dispatch.NavigationTarget);
            OnTrackQuest.Broadcast(Dispatch.NavigationTarget);
        }
        else if(Dispatch.Action==EAetherInspectAction::FocusSkill)Select({Dispatch.NavigationTarget,1},true);
    }
    Refresh();if(!Done&&Notice)Notice->SetText(FText::FromString(Reason));
}
void UAetherSkillTreePage::Cancel(FGuid Token)
{if(Session.GetDraft().IsSet()&&Session.GetDraft()->Token==Token){Session.Back();HideConfirmation();}}
void UAetherSkillTreePage::ShowConfirmation()
{
    if(!Session.GetDraft().IsSet()||!Menu.IsValid())return;
    ModalToken=Menu->PushLayer(TEXT("SkillConfirmation"));
    if(!ModalToken.IsValid()){Session.Back();return;}
    Confirmation->SetDraft(Session.GetDraft().GetValue());ModalShield->SetVisibility(ESlateVisibility::Visible);Confirmation->SetKeyboardFocus();
}
void UAetherSkillTreePage::HideConfirmation()
{
    const auto Token=ModalToken;ModalToken.Invalidate();
    if(Confirmation)Confirmation->InvalidateDraft();if(ModalShield)ModalShield->SetVisibility(ESlateVisibility::Collapsed);
    if(Menu.IsValid()&&Token.IsValid())Menu->DismissLayer(Token);
}
void UAetherSkillTreePage::HandleMenu()
{
    if(bNativeSnapshot&&(!Menu.IsValid()||!NativePawn.IsValid()||Menu->GetBoundPawn()!=NativePawn.Get()))
    {
        // 命令服务已经收到按值复制的请求；换 Pawn 只丢弃本页展示，禁止继续显示旧拥有者数据。
        HideConfirmation();Session=FAetherInspectionSession();Snapshot={};NativePawn.Reset();Selected.Reset();PendingNode.Reset();Refresh();
    }
    if(!Menu.IsValid()||Menu->GetPage()!=EAetherMenuPage::Skills){ClosePresentation();return;}
    if(ModalToken.IsValid()&&!Menu->HasLayer(ModalToken)){if(Session.GetDraft().IsSet())Session.Back();HideConfirmation();}
}
void UAetherSkillTreePage::ClosePresentation()
{
    HideConfirmation();Session.Close();if(Graph)Graph->CancelInteraction();
    if(Details){FAetherInspectionModel Empty;Empty.Message=TEXT("请选择一个技能节点。");Details->SetModel(Empty);}
}
void UAetherSkillTreePage::SelectHotbar(const FAetherInspectRequest& R,const FAetherInspectionAction&,bool)
{if(R.Context.Same(Snapshot.Context))Select({R.Target.DefinitionId,R.Target.SkillRank},true);}
void UAetherSkillTreePage::RenderSources()
{
    if(!Sources)return;constexpr int32 PageSize=3;
    const int32 Pages=FMath::Max(1,(TreeModel.PointSources.Num()+PageSize-1)/PageSize);SourcePage=FMath::Clamp(SourcePage,0,Pages-1);
    FString Text=FString::Printf(TEXT("技能点来源 %d / %d"),SourcePage+1,Pages);
    if(TreeModel.PointSources.IsEmpty())Text+=TEXT("：暂无点数发放记录");
    for(int32 I=SourcePage*PageSize;I<FMath::Min(TreeModel.PointSources.Num(),(SourcePage+1)*PageSize);++I)
        Text+=FString::Printf(TEXT("\n%s：+%d"),*TreeModel.PointSources[I].EventId,TreeModel.PointSources[I].Points);
    Sources->SetText(FText::FromString(Text));
}
void UAetherSkillTreePage::PreviousSources(){--SourcePage;RenderSources();}
void UAetherSkillTreePage::NextSources(){++SourcePage;RenderSources();}
void UAetherSkillTreePage::ResetGraphView(){if(Graph)Graph->ResetView();}

UWidget* UAetherSkillTreePage::GetNavigationFocusTarget() const{return Graph;}

FReply UAetherSkillTreePage::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Session.GetDraft().IsSet()||Event.GetEffectingButton()==EKeys::RightMouseButton)return FReply::Handled();
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}
