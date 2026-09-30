#include "Interaction/AetherDialogueSession.h"
#include "Interaction/AetherNativeInteraction.h"
#include "Characters/AetherFrontierCharacter.h"
#include "World/AetherFrontierProp.h"
#include "Networking/AetherCommandClient.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Definitions/AetherV10Definitions.h"
#include "Combat/AetherCombatComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

bool UAetherDialogueSession::Open(AAetherFrontierCharacter& C,AAetherFrontierProp& T,const FAetherInteractionSelection& S,FString& Why)
{
    auto* PC=GetLocalPlayer()->GetPlayerController(GetWorld());
    if(!PC||!PC->IsLocalController()||PC->GetPawn()!=&C||C.GetController()!=PC||!C.Alive())
    {Why=TEXT("对话只属于当前本地角色。");return false;}
    auto Provider=AetherNativeInteraction::Provider(C,T);auto* Client=GetLocalPlayer()->GetSubsystem<UAetherCommandClient>();
    if(!Provider.IsSet()||!Client->GetChannel().IsValid()||Provider->CheckSelection({Client->GetOwnerIdentity(),S.TargetStableId},S)!=EAetherCommandCode::Applied)
    {Why=TEXT("对话目标已变化。");return false;}
    const auto Offers=Provider->Query({Client->GetOwnerIdentity(),S.TargetStableId});
    const auto* Offer=Offers.FindByPredicate([&](const auto& O){return O.ActionId==S.ActionId&&O.Availability==EAetherOfferAvailability::TalkOnly;});
    if(!Offer||Offer->DialogueId.IsEmpty())return false;
    const FString StartNode=Offer->DialogueId;
    Close();ReleaseCamera(true);
    Player=&C;Target=&T;OwningController=PC;Shown=S;Channel=Client->GetChannel();Node=StartNode;Signature.Reset();
    DamageSerial=C.CombatRuntime->DamageReceivedCount;
    auto* Menu=GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>();Menu->OpenPage(EAetherMenuPage::Dialogue);
    if(Menu->GetPage()!=EAetherMenuPage::Dialogue||!Refresh()){Close();ReleaseCamera(true);Why=TEXT("对话或镜头尚未就绪。");return false;}
    Why.Reset();return true;
}
void UAetherDialogueSession::Close()
{
    const bool HadSession=View.IsSet()||Target.IsValid();ReleaseCamera(false);
    View.Reset();Playback.Reset();Player.Reset();Target.Reset();OwningController.Reset();Node.Reset();Signature.Reset();Channel.Invalidate();
    if(!HadSession)return;
    ++Version;OnChanged.Broadcast();
    auto* Menu=GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>();
    if(Menu->GetPage()==EAetherMenuPage::Dialogue)Menu->Close();
    // Intentionally no CommandClient cancellation: already submitted services finish authoritatively.
}
bool UAetherDialogueSession::ContextValid() const
{
    auto* C=Player.Get();auto* T=Target.Get();auto* PC=OwningController.Get();
    const auto* Client=GetLocalPlayer()->GetSubsystem<UAetherCommandClient>();
    return C&&T&&PC&&!C->IsActorBeingDestroyed()&&!T->IsActorBeingDestroyed()&&C->Alive()&&
        PC->IsLocalController()&&GetLocalPlayer()->GetPlayerController(GetWorld())==PC&&PC->GetPawn()==C&&C->GetController()==PC&&
        C->CombatRuntime->DamageReceivedCount==DamageSerial&&Client->GetChannel()==Channel&&Channel.IsValid()&&
        GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>()->GetPage()==EAetherMenuPage::Dialogue&&
        T->InteractionRevision==Shown.InteractionRevision&&T->Spec.Id.ToString().Equals(Shown.TargetStableId,ESearchCase::CaseSensitive);
}
const FAetherDialoguePresentation* UAetherDialogueSession::GetPresentation() const
{return View.IsSet()?FAetherV10Definitions::Get().Interactions.Presentations.Find(View->PresentationId):nullptr;}
FString UAetherDialogueSession::GetSubtitle() const
{
    if(!View.IsSet())return {};
    if(View->Lines.IsValidIndex(Playback.LineIndex()))return View->Lines[Playback.LineIndex()].Text;
    return View->Lines.IsEmpty()?FString():View->Lines.Last().Text;
}
bool UAetherDialogueSession::Refresh()
{
    if(!ContextValid())return false;
    auto* C=Player.Get();auto* T=Target.Get();auto* Client=GetLocalPlayer()->GetSubsystem<UAetherCommandClient>();
    auto Provider=AetherNativeInteraction::Provider(*C,*T);if(!Provider.IsSet())return false;
    const FAetherInteractionQuery Q{Client->GetOwnerIdentity(),Shown.TargetStableId};
    auto Next=Provider->QueryDialogue(Q,Node);if(!Next.IsSet())return false;
    const auto Offers=Provider->Query(Q);if(Offers.IsEmpty())return false;
    const bool NewNode=!View.IsSet()||!View->NodeId.Equals(Node,ESearchCase::CaseSensitive);
    if(NewNode)
    {
        const auto* Presentation=FAetherV10Definitions::Get().Interactions.Presentations.Find(Next->PresentationId);FString Why;
        if(!Presentation||!Playback.Start(Next->Lines,Presentation->bAllowAdvance,Presentation->bAllowSkip)||!BeginCamera(*Presentation,Why))
        {UE_LOG(LogTemp,Warning,TEXT("AETHER_DIALOGUE_PRESENTATION_UNAVAILABLE %s"),*Why);return false;}
    }
    Shown.ProfileRevision=Offers[0].ProfileRevision;Shown.WorldRevision=Offers[0].WorldRevision;
    FString Key=LexToString(Shown.ProfileRevision)+TEXT("|")+LexToString(Shown.WorldRevision)+TEXT("|")+Node;
    for(const auto& Choice:Next->Choices)Key+=TEXT("|")+Choice.ActionId+TEXT(":")+LexToString(uint8(Choice.Availability))+TEXT(":")+Choice.ReasonId;
    if(Key!=Signature){Signature=MoveTemp(Key);View=MoveTemp(Next);++Version;OnChanged.Broadcast();}
    return true;
}
bool UAetherDialogueSession::Advance(uint64 ShownVersion)
{
    if(!View.IsSet()||Version!=ShownVersion)return false;
    if(!Refresh()){Close();return false;}if(Version!=ShownVersion||!Playback.Advance())return false;
    ++Version;OnChanged.Broadcast();return true;
}
bool UAetherDialogueSession::Skip(uint64 ShownVersion)
{
    if(!View.IsSet()||Version!=ShownVersion)return false;
    if(!Refresh()){Close();return false;}if(Version!=ShownVersion||!Playback.Skip())return false;
    ++Version;OnChanged.Broadcast();return true;
}
bool UAetherDialogueSession::Choose(int32 Index,uint64 ShownVersion,FString& Why,FGuid* SubmittedCommandId)
{
    if(SubmittedCommandId)SubmittedCommandId->Invalidate();
    if(!View.IsSet()||Playback.Phase()!=EAetherDialoguePlaybackPhase::Choices||Version!=ShownVersion||!View->Choices.IsValidIndex(Index))
    {Why=TEXT("选项已更新或字幕尚未结束，请前进或跳过字幕。");return false;}
    const auto Choice=View->Choices[Index];const auto Selection=Shown;
    if(!Refresh()){Close();Why=TEXT("对话已中断。");return false;}
    if(Version!=ShownVersion){Why=TEXT("条件已变化，请查看更新后的选项。");return false;}
    if(Choice.Availability!=EAetherOfferAvailability::Available&&Choice.Availability!=EAetherOfferAvailability::TalkOnly)
    {Why=TEXT("尚未满足这项服务的条件。");return false;}
    if(!Choice.ActionId.IsEmpty())
    {
        auto S=Selection;S.ActionId=Choice.ActionId;
        if(!Player.IsValid()||!AetherNativeInteraction::Submit(*Player.Get(),S,Why,SubmittedCommandId))return false;
    }
    if(!Choice.NextNodeId.IsEmpty()){Node=Choice.NextNodeId;if(!Refresh())Close();}
    else if(Choice.ActionId.IsEmpty())Close();
    return true;
}
void UAetherDialogueSession::Tick(float Dt)
{
    // Interruption checks precede subtitle advancement on every frame, including during camera return.
    if(View.IsSet())
    {
        if(!Refresh())Close();
        else if(Playback.Tick(Dt)){++Version;OnChanged.Broadcast();}
    }
    TickCamera(Dt);
}
TStatId UAetherDialogueSession::GetStatId() const
{RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherDialogueSession,STATGROUP_Tickables);}
void UAetherDialogueSession::Deinitialize(){Close();ReleaseCamera(true);OnChanged.Clear();Super::Deinitialize();}
