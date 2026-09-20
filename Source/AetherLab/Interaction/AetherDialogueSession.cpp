#include "Interaction/AetherDialogueSession.h"
#include "Interaction/AetherNativeInteraction.h"
#include "Characters/AetherFrontierCharacter.h"
#include "World/AetherFrontierProp.h"
#include "Networking/AetherCommandClient.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Engine/LocalPlayer.h"

bool UAetherDialogueSession::Open(AAetherFrontierCharacter& C,AAetherFrontierProp& T,const FAetherInteractionSelection& S,FString& Why)
{
    auto Provider=AetherNativeInteraction::Provider(C,T);auto* Client=GetLocalPlayer()->GetSubsystem<UAetherCommandClient>();
    if(!Provider.IsSet()||!Client->GetChannel().IsValid()||Provider->CheckSelection({Client->GetOwnerIdentity(),S.TargetStableId},S)!=EAetherCommandCode::Applied)
    {Why=TEXT("对话目标已变化。");return false;}
    const auto Offers=Provider->Query({Client->GetOwnerIdentity(),S.TargetStableId});
    const auto* Offer=Offers.FindByPredicate([&](const auto& O){return O.ActionId==S.ActionId&&O.Availability==EAetherOfferAvailability::TalkOnly;});
    if(!Offer||Offer->DialogueId.IsEmpty())return false;
    Player=&C;Target=&T;Shown=S;Channel=Client->GetChannel();Node=Offer->DialogueId;Signature.Reset();
    GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>()->OpenPage(EAetherMenuPage::Dialogue);
    if(!Refresh()){Close();Why=TEXT("目前无法交谈。");return false;}
    Why.Reset();return true;
}
void UAetherDialogueSession::Close()
{
    if(!View.IsSet()&&!Target.IsValid())return;
    View.Reset();Player.Reset();Target.Reset();Node.Reset();Signature.Reset();Channel.Invalidate();++Version;OnChanged.Broadcast();
    auto* Menu=GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>();
    if(Menu->GetPage()==EAetherMenuPage::Dialogue)Menu->Close();
}
bool UAetherDialogueSession::Refresh()
{
    auto* C=Player.Get();auto* T=Target.Get();auto* Client=GetLocalPlayer()->GetSubsystem<UAetherCommandClient>();
    if(!C||!T||GetLocalPlayer()->GetPlayerController(GetWorld())!=C->GetController()||Client->GetChannel()!=Channel||
        T->InteractionRevision!=Shown.InteractionRevision||T->Spec.Id.ToString()!=Shown.TargetStableId)return false;
    auto Provider=AetherNativeInteraction::Provider(*C,*T);if(!Provider.IsSet())return false;
    const FAetherInteractionQuery Q{Client->GetOwnerIdentity(),Shown.TargetStableId};
    auto Next=Provider->QueryDialogue(Q,Node);if(!Next.IsSet())return false;
    const auto Offers=Provider->Query(Q);if(Offers.IsEmpty())return false;
    Shown.ProfileRevision=Offers[0].ProfileRevision;Shown.WorldRevision=Offers[0].WorldRevision;
    FString Key=LexToString(Shown.ProfileRevision)+TEXT("|")+LexToString(Shown.WorldRevision)+TEXT("|")+Node;
    for(const auto& Choice:Next->Choices)Key+=TEXT("|")+Choice.ActionId+TEXT(":")+LexToString(uint8(Choice.Availability))+TEXT(":")+Choice.ReasonId;
    if(Key!=Signature){Signature=MoveTemp(Key);View=MoveTemp(Next);++Version;OnChanged.Broadcast();}
    return true;
}
bool UAetherDialogueSession::Choose(int32 Index,uint64 ShownVersion,FString& Why)
{
    if(!View.IsSet()||Version!=ShownVersion||!View->Choices.IsValidIndex(Index)){Why=TEXT("选项已更新，请重新选择。");return false;}
    const auto Choice=View->Choices[Index];const auto Selection=Shown;
    // 刷新只能判定旧选择是否失效，不能悄悄把旧按钮升级成新版本的操作。
    if(!Refresh()||Version!=ShownVersion){Why=TEXT("条件已变化，请查看更新后的选项。");return false;}
    if(Choice.Availability!=EAetherOfferAvailability::Available&&Choice.Availability!=EAetherOfferAvailability::TalkOnly)
    {Why=TEXT("尚未满足这项服务的条件。");return false;}
    if(!Choice.ActionId.IsEmpty())
    {
        auto S=Selection;S.ActionId=Choice.ActionId;
        if(!Player.IsValid()||!AetherNativeInteraction::Submit(*Player.Get(),S,Why))return false;
    }
    if(!Choice.NextNodeId.IsEmpty()){Node=Choice.NextNodeId;if(!Refresh())Close();}
    else if(Choice.ActionId.IsEmpty())Close();
    return true;
}
void UAetherDialogueSession::Tick(float Dt)
{
    PollElapsed+=Dt;if(PollElapsed<.2f)return;PollElapsed=0;
    if(GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>()->GetPage()!=EAetherMenuPage::Dialogue||!Refresh())Close();
}
TStatId UAetherDialogueSession::GetStatId() const
{RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherDialogueSession,STATGROUP_Tickables);}
void UAetherDialogueSession::Deinitialize(){Close();OnChanged.Clear();Super::Deinitialize();}
