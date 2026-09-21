#include "Skills/AetherSkillTreePage.h"
#include "UI/AetherWidgetAssets.h"
#include "UI/AetherMenuRoot.h"
#include "Skills/AetherSkillGraphWidget.h"
#include "Skills/AetherSkillHotbarCell.h"
#include "Inspection/AetherInspectionWidgets.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Framework/AetherFrontier.h"
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
#include "Networking/AetherCommandClient.h"
#include "Definitions/AetherV10Definitions.h"
#include "InputCoreTypes.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "Quests/AetherGuide.h"
#include "TimerManager.h"

TSharedRef<SWidget> UAetherSkillTreePage::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
    if(WidgetTree->RootWidget)AetherWidgetAssets::BindDesigner(*this,*WidgetTree);
    if(!WidgetTree->RootWidget)
    {
        auto* Overlay=WidgetTree->ConstructWidget<UOverlay>();WidgetTree->RootWidget=Overlay;
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();Overlay->AddChildToOverlay(Root);
        auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Header);
        Points=WidgetTree->ConstructWidget<UTextBlock>();Header->AddChildToHorizontalBox(Points)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Reset=WidgetTree->ConstructWidget<UButton>();auto* ResetText=WidgetTree->ConstructWidget<UTextBlock>();
        ResetText->SetText(FText::FromString(TEXT("重置视图")));Reset->SetContent(ResetText);Header->AddChildToHorizontalBox(Reset);
        Reset->OnClicked.AddDynamic(this,&UAetherSkillTreePage::ResetGraphView);
        auto* Retry=WidgetTree->ConstructWidget<UButton>();auto* RetryText=WidgetTree->ConstructWidget<UTextBlock>();
        RetryText->SetText(FText::FromString(TEXT("同步 / 重试原请求")));Retry->SetContent(RetryText);Header->AddChildToHorizontalBox(Retry);
        Retry->OnClicked.AddDynamic(this,&UAetherSkillTreePage::RetryNativeCommand);
        Notice=WidgetTree->ConstructWidget<UTextBlock>();Notice->SetAutoWrapText(true);Root->AddChildToVerticalBox(Notice);
        auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Graph=WidgetTree->ConstructWidget<UAetherSkillGraphWidget>();Graph->OnNodeSelected.AddUObject(this,&UAetherSkillTreePage::Select);
        auto* GraphSlot=Body->AddChildToHorizontalBox(Graph);FSlateChildSize GraphSize(ESlateSizeRule::Fill);GraphSize.Value=.62f;GraphSlot->SetSize(GraphSize);
        Details=CreateWidget<UAetherInspectionCard>(this,AetherWidgetAssets::Class<UAetherInspectionCard>());
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
        Confirmation=CreateWidget<UAetherInspectionConfirmation>(this,AetherWidgetAssets::Class<UAetherInspectionConfirmation>());
    }
    Refresh();
    if(!Confirmation)Confirmation=CreateWidget<UAetherInspectionConfirmation>(this,AetherWidgetAssets::Class<UAetherInspectionConfirmation>());
    AetherWidgetAssets::BindButton(*this,TEXT("ResetButton"),TEXT("ResetGraphView"));
    AetherWidgetAssets::BindButton(*this,TEXT("RetryButton"),TEXT("RetryNativeCommand"));
    AetherWidgetAssets::BindButton(*this,TEXT("PreviousButton"),TEXT("PreviousSources"));
    AetherWidgetAssets::BindButton(*this,TEXT("NextButton"),TEXT("NextSources"));
    return Super::RebuildWidget();
}
void UAetherSkillTreePage::NativeConstruct()
{
    if(Details){Details->OnActionRequested.RemoveAll(this);Details->OnDismissRequested.RemoveAll(this);
        Details->OnActionRequested.AddUObject(this,&UAetherSkillTreePage::RequestAction);Details->OnDismissRequested.AddUObject(this,&UAetherSkillTreePage::ClosePresentation);}
    if(Graph){Graph->OnNodeSelected.RemoveAll(this);Graph->OnNodeSelected.AddUObject(this,&UAetherSkillTreePage::Select);}
    Super::NativeConstruct();
    if(auto* LP=GetOwningLocalPlayer())Menu=LP->GetSubsystem<UAetherMenuSubsystem>();
    if(Menu.IsValid()){Menu->OnChanged.RemoveAll(this);Menu->OnChanged.AddUObject(this,&UAetherSkillTreePage::HandleMenu);}
    if(auto* LP=GetOwningLocalPlayer())CommandClient=LP->GetSubsystem<UAetherCommandClient>();
    if(CommandClient.IsValid())
    {
        CommandClient->OnChanged.RemoveAll(this);CommandClient->OnResult.RemoveAll(this);
        CommandClient->OnChanged.AddUObject(this,&UAetherSkillTreePage::HandleNativeProfile);
        CommandClient->OnResult.AddUObject(this,&UAetherSkillTreePage::ReceiveReceipt);
        OnCommandReady.RemoveAll(this);OnCommandReady.AddUObject(this,&UAetherSkillTreePage::DispatchNativeCommand);
        HandleNativeProfile();
    }
    HandleMenu();
}
void UAetherSkillTreePage::NativeDestruct()
{
    if(Menu.IsValid())Menu->OnChanged.RemoveAll(this);
    if(CommandClient.IsValid()){CommandClient->OnChanged.RemoveAll(this);CommandClient->OnResult.RemoveAll(this);}
    if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(ViewTimer);
    CommandClient.Reset();ClosePresentation();Menu.Reset();OnCommandReady.Clear();OnTrackQuest.Clear();Super::NativeDestruct();
}
void UAetherSkillTreePage::HandleNativeProfile()
{
    if(!CommandClient.IsValid())return;
    const auto& P=CommandClient->GetProfile();
    if(!P.IsSet())
    {
        if(bNativeSnapshot||CommandClient->GetChannel().IsValid())
        {
            HideConfirmation();Session=FAetherInspectionSession();Snapshot={};Selected.Reset();PendingNode.Reset();
            NativePawn.Reset();bNativeSnapshot=false;LegacyRevision=-1;Refresh();
        }
        return;
    }
    FAetherInspectionSnapshot S;
    S.Context.OwnerIdentity=P->CharacterId;S.Context.SessionId=CommandClient->GetChannel();
    S.ProfileRevision=P->Revision;S.Inventory=P->Inventory;S.Skills=P->Skills;
    S.SkillContext.CharacterLevel=FMath::Clamp(1+P->Experience/200,1,100);
    for(const auto& Claim:P->Claims)S.SkillContext.CompletedQuests.Add(Claim);
    auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());auto* PS=C?C->ProfileState():nullptr;
    if(C)
    {
        S.SkillContext.bInCombat=C->HasRecentCombat(8);S.SkillContext.bCasting=C->CastLockUntil>C->CombatTime();S.SkillContext.bCoolingDown=S.SkillContext.bCasting;
        S.bCanAct=C->Ready()&&!CommandClient->HasPending();
        if(auto* Registry=C->GetWorld()->GetSubsystem<UAetherNearbyRegistry>())
            for(const auto& Weak:Registry->Nearby(C->GetActorLocation(),250))
                if(auto* Teacher=Cast<AAetherFrontierProp>(Weak.Get());Teacher&&Teacher->Service=="Teacher"&&Teacher->bEnabled)
                    if(AetherGuide::QueryTarget(C,Teacher).Prop==Teacher){S.SkillContext.bAtResetService=true;break;}
        if(PS&&PS->SkillGrants.ProfileRevision==P->Revision)S.ExternalGrants=PS->GetNativeSkillGrants();
    }
    FString Key=S.Context.SessionId.ToString()+FString::Printf(TEXT("|%lld|%d%d%d%d|%lld"),P->Revision,S.SkillContext.bAtResetService,S.SkillContext.bInCombat,S.SkillContext.bCasting,CommandClient->HasPending(),PS?PS->SkillGrants.ProfileRevision:-1);
    if(PS)Key+=TEXT("|grants:")+FString::FromInt(PS->SkillGrants.Sequence);
    for(const auto& G:S.ExternalGrants)Key+=TEXT("|")+G.SourceId+TEXT(":")+G.SkillId+FString::FromInt(G.Rank);
    if(Key==NativeSnapshotKey)return;NativeSnapshotKey=Key;S.Context.SnapshotRevision=++ViewGeneration;
    PublishSnapshot(S);
}
void UAetherSkillTreePage::DispatchNativeCommand(const FAetherInspectionDispatch& D)
{
    if(!D.Command.IsSet())return;
    FString Reason;
    if(!CommandClient.IsValid()||!CommandClient->Submit(Snapshot.Context.SessionId,Snapshot.Context.OwnerIdentity,D.CommandBytes,Reason))
    {
        // Submit 返回 false 保证未发送；本地失败不能伪装为服务器事务回执。
        Session.RejectBeforeSend(D.Command->CommandId);Session.Refresh(Snapshot,FAetherV10Definitions::Get().Items,FAetherSkillDefinitionsV10::Get());
        Refresh();if(Notice)Notice->SetText(FText::FromString(Reason.IsEmpty()?TEXT("命令通道不可用。"):Reason));
    }
}
void UAetherSkillTreePage::RetryNativeCommand()
{
    if(!CommandClient.IsValid())return;
    if(!CommandClient->HasPending()||!CommandClient->RetryPending())CommandClient->RequestSnapshot();
}
void UAetherSkillTreePage::PublishSnapshot(const FAetherInspectionSnapshot& S)
{
    const bool NewOwner=!Snapshot.Context.OwnerIdentity.Equals(S.Context.OwnerIdentity,ESearchCase::CaseSensitive)||Snapshot.Context.SessionId!=S.Context.SessionId;
    if(NewOwner){HideConfirmation();Session=FAetherInspectionSession();Selected.Reset();SourcePage=0;}
    bNativeSnapshot=true;NativePawn=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());Snapshot=S;LegacySource.Reset();LegacyRevision=-1;
    Session.Refresh(Snapshot,FAetherV10Definitions::Get().Items,FAetherSkillDefinitionsV10::Get());
    if(!Session.GetDraft().IsSet())HideConfirmation();
    Refresh();
}
void UAetherSkillTreePage::ReceiveReceipt(const FAetherCommandResult& Result)
{
    if(Session.Acknowledge(Result))
    {Session.Refresh(Snapshot,FAetherV10Definitions::Get().Items,FAetherSkillDefinitionsV10::Get());Refresh();}
}
void UAetherSkillTreePage::SetLegacySource(AAetherFrontierCharacter* C)
{
    if(bNativeSnapshot||(CommandClient.IsValid()&&CommandClient->GetChannel().IsValid()))return;
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
    Session.Refresh(Snapshot,FAetherV10Definitions::Get().Items,FAetherSkillDefinitionsV10::Get());Refresh();
}
void UAetherSkillTreePage::ApplyCommandAvailability(FAetherInspectionModel& M) const
{
    if(bNativeSnapshot&&OnCommandReady.IsBound())
    {
        if(!CommandClient.IsValid()||!CommandClient->HasPending())return;
        for(auto& A:M.Actions)if(A.Kind!=EAetherInspectAction::FocusSkill&&A.Kind!=EAetherInspectAction::TrackQuest)
        {A.bEnabled=false;A.DisabledReason=TEXT("原请求尚未确认，请等待或点击重试。");}
        return;
    }
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
        bNativeSnapshot?(CommandClient.IsValid()&&CommandClient->HasPending()?TEXT("有待确认请求；重连后可点击重试，仅查询或重发原命令。"):TEXT("点击查看 · 右键详情 · 空白拖动 / 滚轮缩放 · 方向键或手柄方向键选择")):
        TEXT("当前展示已学基础能力和成长路线。技能成长暂不可用；基础能力仍可向导师学习。")));
    if(Session.GetDetails().IsSet()){auto M=Session.GetDetails().GetValue();ApplyCommandAvailability(M);Details->SetModel(M);}
    else if(TreeModel.bValid&&!TreeModel.Nodes.IsEmpty())Select(Selected.IsSet()?Selected.GetValue():TreeModel.Nodes[0].Identity,false);
    else Details->SetModel(FAetherInspectionModel());
    Hotbar->ClearChildren();
    for(const auto& H:TreeModel.Hotbar)
    {
        auto* B=WidgetTree->ConstructWidget<UAetherSkillHotbarCell>();
        FAetherInspectTarget T;T.Kind=EAetherInspectTarget::SkillNode;T.DefinitionId=H.SkillId;T.SkillRank=FMath::Max(1,H.EffectiveRank);
        B->Present(AetherInspection::Pin(Snapshot,T),H.Slot,FString::Printf(TEXT("%d · %s%s"),H.Slot+1,H.Title.IsEmpty()?TEXT("拖入主动技能"):*H.Title,H.SkillId.IsEmpty()?TEXT(""):H.bAvailable?TEXT(" · 可用"):TEXT(" · 未授权")));
        B->OnBind.BindUObject(this,&UAetherSkillTreePage::DropSkill);B->OnInspect.BindUObject(this,&UAetherSkillTreePage::InspectHotbar);
        Hotbar->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
    RenderSources();
}
void UAetherSkillTreePage::Select(const FAetherSkillNodeIdentity& Node,bool FocusDetail)
{
    if(!TreeModel.Nodes.ContainsByPredicate([&](const auto& N){return N.Identity==Node;}))return;
    HideConfirmation();Selected=Node;FAetherInspectTarget T;T.Kind=EAetherInspectTarget::SkillNode;T.DefinitionId=Node.SkillId;T.SkillRank=Node.Rank;
    Session.OpenDetails(T,Snapshot,FAetherV10Definitions::Get().Items,FAetherSkillDefinitionsV10::Get());
    if(Details){auto M=Session.GetDetails().GetValue();ApplyCommandAvailability(M);Details->SetModel(M);if(FocusDetail)Details->SetKeyboardFocus();}
    if(Graph)Graph->SelectNode(Node);
}
void UAetherSkillTreePage::RequestAction(const FAetherInspectRequest& R,const FAetherInspectionAction& A)
{
    if(!R.Context.Same(Snapshot.Context)||!Session.GetDetails().IsSet()||
        !R.Target.DefinitionId.Equals(Session.GetDetails()->Request.Target.DefinitionId,ESearchCase::CaseSensitive)||
        R.Target.SkillRank!=Session.GetDetails()->Request.Target.SkillRank)return;
    const bool bNavigationAction=A.Kind==EAetherInspectAction::FocusSkill||A.Kind==EAetherInspectAction::TrackQuest;
    if(!bNavigationAction&&(!bNativeSnapshot||!OnCommandReady.IsBound()||(CommandClient.IsValid()&&CommandClient->HasPending())))return;
    const FGuid Token=Session.BeginAction(A.Kind,A.Argument);if(!Token.IsValid())return;
    if(A.bNeedsConfirmation||A.MaxQuantity>1)ShowConfirmation();else Confirm(Token,1);
}
void UAetherSkillTreePage::Confirm(FGuid Token,int32 Count)
{
    HandleNativeProfile();
    // 旧确认回调不能关闭后来打开的新弹窗；先检查令牌再执行或拆除任何控件。
    if(!Session.GetDraft().IsSet()||Session.GetDraft()->Token!=Token)return;
    const auto Kind=Session.GetDraft()->Action.Kind;
    if(Kind!=EAetherInspectAction::FocusSkill&&Kind!=EAetherInspectAction::TrackQuest&&(!bNativeSnapshot||!OnCommandReady.IsBound()))
    {Session.Back();HideConfirmation();Refresh();return;}
    FAetherInspectionDispatch Dispatch;FString Reason;
    const bool Done=Session.Confirm(Token,Count,Snapshot,FAetherV10Definitions::Get().Items,FAetherSkillDefinitionsV10::Get(),Dispatch,Reason);
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
            if(auto* C=bNativeSnapshot?NativePawn.Get():LegacySource.Get())C->TrackedQuest=FName(*Dispatch.NavigationTarget);
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
    auto* Root=UAetherMenuRoot::Find(*this);
    if(!Root||!Root->PushModal(ModalToken,Confirmation)){Session.Back();HideConfirmation();return;}
    Confirmation->OnConfirmed.RemoveAll(this);Confirmation->OnCancelled.RemoveAll(this);
    Confirmation->OnConfirmed.AddUObject(this,&UAetherSkillTreePage::Confirm);Confirmation->OnCancelled.AddUObject(this,&UAetherSkillTreePage::Cancel);
    Confirmation->SetDraft(Session.GetDraft().GetValue());Confirmation->SetUserFocus(GetOwningPlayer());
}
void UAetherSkillTreePage::HideConfirmation()
{
    const auto Token=ModalToken;ModalToken.Invalidate();
    if(Confirmation)Confirmation->InvalidateDraft();if(auto* Root=UAetherMenuRoot::Find(*this))Root->PopModal(Token);
    if(Menu.IsValid()&&Token.IsValid())Menu->DismissLayer(Token);
}
void UAetherSkillTreePage::HandleMenu()
{
    if(bNativeSnapshot&&(!Menu.IsValid()||!NativePawn.IsValid()||Menu->GetBoundPawn()!=NativePawn.Get()))
    {
        // 命令服务已经收到按值复制的请求；换 Pawn 只丢弃本页展示，禁止继续显示旧拥有者数据。
        HideConfirmation();Session=FAetherInspectionSession();Snapshot={};NativePawn.Reset();Selected.Reset();PendingNode.Reset();Refresh();
    }
    if(!Menu.IsValid()||Menu->GetPage()!=EAetherMenuPage::Skills){if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(ViewTimer);ClosePresentation();return;}
    if(GetWorld()&&!GetWorld()->GetTimerManager().IsTimerActive(ViewTimer))GetWorld()->GetTimerManager().SetTimer(ViewTimer,this,&UAetherSkillTreePage::HandleNativeProfile,.25f,true);
    HandleNativeProfile();
    if(ModalToken.IsValid()&&!Menu->HasLayer(ModalToken)){if(Session.GetDraft().IsSet())Session.Back();HideConfirmation();}
}
FReply UAetherSkillTreePage::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Escape||E.GetKey()==EKeys::Gamepad_FaceButton_Right)
    {
        if(!E.IsRepeat()){if(Session.Back()){ClosePresentation();}else if(Menu.IsValid())Menu->Back();}
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(G,E);
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

void UAetherSkillTreePage::InspectHotbar(const FAetherInspectRequest& R)
{if(R.Context.Same(Snapshot.Context)&&!R.Target.DefinitionId.IsEmpty())Select({R.Target.DefinitionId,R.Target.SkillRank},true);}
void UAetherSkillTreePage::DropSkill(const FAetherInspectRequest& R,int32 SlotValue)
{
    if(!R.Context.Same(Snapshot.Context)||SlotValue<0||SlotValue>=FAetherSkillStateV10::HotbarCapacity||ModalToken.IsValid()||
        !CommandClient.IsValid()||CommandClient->HasPending())return;
    const auto& D=FAetherSkillDefinitionsV10::Get();const auto* Def=D.Skills.Find(R.Target.DefinitionId);
    if(!Def||!Def->bActive||Snapshot.Skills.EffectiveRank(Def->SkillId,Snapshot.ExternalGrants)<R.Target.SkillRank)return;
    Select({R.Target.DefinitionId,R.Target.SkillRank},false);
    const FString Argument=FString::Printf(TEXT("Hotbar.%d"),SlotValue+1);
    const auto Token=Session.BeginAction(EAetherInspectAction::BindHotbar,Argument);
    if(Token.IsValid())Confirm(Token,1);
}
