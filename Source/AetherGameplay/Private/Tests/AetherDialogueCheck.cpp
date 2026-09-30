#include "Framework/AetherFrontier.h"
#include "Framework/AetherPlayerController.h"
#include "Interaction/AetherDialogueSession.h"
#include "Interaction/AetherDialogueCameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Interaction/AetherNativeInteraction.h"
#include "Networking/AetherCommandClient.h"
#include "Networking/AetherCommandRuntime.h"
#include "Commands/AetherServerFactCoordinator.h"
#include "Definitions/AetherV10Definitions.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Combat/AetherCombatComponent.h"
#include "Camera/CameraActor.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#if !UE_BUILD_SHIPPING
namespace
{
struct FDialogueCheckState
{
    TWeakObjectPtr<AAetherFrontierMode> Mode;
    TWeakObjectPtr<UAetherCommandClient> Client;
    FDelegateHandle Handle;
    FGuid Channel,ExpectedCommandId;
    TOptional<FAetherCommandResult> Reply;
    int32 Stage=0;
    double Started=0,Next=0;
    bool Done=false,Initialized=false;
};
FDialogueCheckState DialogueCheck;
}
#endif
void AAetherFrontierMode::CheckDialogue()
{
#if !UE_BUILD_SHIPPING
    auto& S=DialogueCheck;const double Now=FPlatformTime::Seconds();
    if(S.Mode.Get()!=this){if(S.Client.IsValid())S.Client->OnResult.Remove(S.Handle);S={};S.Mode=this;S.Started=Now;}
    if(S.Done)return;
    const auto Finish=[&](bool Pass,const FString& Why)
    {
        S.Done=true;if(S.Client.IsValid())S.Client->OnResult.Remove(S.Handle);
        UE_LOG(LogTemp,Display,TEXT("AETHER_DIALOGUE_%s stage=%d scope=local_dialogue_service synthetic_setup=true reason=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),S.Stage,*Why);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    const auto Check=[&](bool Pass,const TCHAR* Why)
    {UE_LOG(LogTemp,Display,TEXT("DIALOGUE_CHECK %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),Why);if(!Pass)Finish(false,Why);return Pass;};
    if(Now-S.Started>180){Finish(false,TEXT("Dialogue probe timed out"));return;}
    auto* C=Cast<AAetherFrontierCharacter>(UGameplayStatics::GetPlayerPawn(this,0));auto* PC=C?Cast<AAetherPlayerController>(C->GetController()):nullptr;
    auto* LP=PC?PC->GetLocalPlayer():nullptr;auto* Client=LP?LP->GetSubsystem<UAetherCommandClient>():nullptr;
    auto* Session=LP?LP->GetSubsystem<UAetherDialogueSession>():nullptr;auto* Menu=LP?LP->GetSubsystem<UAetherMenuSubsystem>():nullptr;
    auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>();
    if(!C||!PC||!PC->HasAuthority()||!Session||!Client||!Client->GetChannel().IsValid()||!Client->GetProfile().IsSet()||!Runtime||!Runtime->HasBackend())return;
    const auto& P=Client->GetProfile().GetValue();const auto& D=FAetherV10Definitions::Get();
    if(!S.Initialized)
    {
        FGuid Prefix;const FString Stem=TEXT("AetherDialogueCheck_");
        if(!Check(SavePrefix.StartsWith(Stem,ESearchCase::CaseSensitive)&&FGuid::ParseExact(SavePrefix.Mid(Stem.Len()),EGuidFormats::Digits,Prefix)&&P.Revision==0&&P.Claims.IsEmpty(),TEXT("Fresh isolated dialogue namespace required")))return;
        S.Initialized=true;S.Client=Client;S.Channel=Client->GetChannel();
        S.Handle=Client->OnResult.AddWeakLambda(this,[](const FAetherCommandResult& R)
        {auto& State=DialogueCheck;if(State.ExpectedCommandId.IsValid()&&State.Client.IsValid()&&State.Client->GetChannel()==State.Channel&&R.CommandId==State.ExpectedCommandId)State.Reply=R;});
    }
    if(Client!=S.Client.Get()||Client->GetChannel()!=S.Channel){Finish(false,TEXT("Unexpected owner/channel replacement"));return;}
    Client->RequestSnapshot();auto* Registrar=Prop(TEXT("Registrar"));if(!Registrar)return;
    C->SetActorLocation(Registrar->GetActorLocation()+FVector(-150,0,0));
    const auto Open=[&](AAetherFrontierProp& Target)
    {
        auto* Server=C->ProfileState()?C->ProfileState()->GetNativeProfile():nullptr;if(!Server||Server->Revision!=P.Revision)return false;
        auto Provider=AetherNativeInteraction::Provider(*C,Target);if(!Provider.IsSet())return false;
        const auto Offers=Provider->Query({Client->GetOwnerIdentity(),Target.Spec.Id.ToString()});
        const auto* Talk=Offers.FindByPredicate([](const auto& O){return O.Availability==EAetherOfferAvailability::TalkOnly;});if(!Talk)return false;
        const FAetherInteractionSelection Selection{Talk->TargetStableId,Talk->ActionId,Talk->ProfileRevision,Talk->WorldRevision,Talk->TargetRevision};FString Why;
        return Session->Open(*C,Target,Selection,Why);
    };
    const auto SettleClose=[&](){Session->Tick(4);};
    const auto LocalCameras=[&](){int32 Count=0;for(TActorIterator<ACameraActor> It(GetWorld());It;++It)if(It->GetOwner()==PC&&!It->IsActorBeingDestroyed())++Count;return Count;};
    if(S.Stage==0)
    {
        const auto Before=PC->GetViewTarget();const int32 Cameras=LocalCameras();const int32 Pending=Client->PendingCommandCount();
        if(!Open(*Registrar))return;
        auto* Shot=Cast<ACameraActor>(PC->GetViewTarget());FString Why;const uint64 Old=Session->GetVersion();
        if(!Check(Shot&&Shot->GetOwner()==PC&&!Shot->GetIsReplicated(),TEXT("Production Open creates one owner-local nonreplicated shot"))||
           !Check(Session->GetPlaybackPhase()==EAetherDialoguePlaybackPhase::Speaking&&!Session->Choose(0,Old,Why),TEXT("Service cannot execute before subtitle completion"))||
           !Check(Session->Advance(Old)&&!Session->Advance(Old),TEXT("Advance consumes its exact shown version once"))||
           !Check(Session->Skip(Session->GetVersion())&&Session->GetPlaybackPhase()==EAetherDialoguePlaybackPhase::Choices&&Client->PendingCommandCount()==Pending,TEXT("Skipping display never submits a command")))return;
        Menu->OpenPage(EAetherMenuPage::System);Session->Tick(0);SettleClose();
        if(!Check(!Session->GetView().IsSet()&&PC->GetViewTarget()==Before&&LocalCameras()==Cameras&&Menu->GetPage()==EAetherMenuPage::System,TEXT("Menu interruption restores prior viewpoint without closing the new page")))return;
        if(!Open(*Registrar))return;
        ++C->CombatRuntime->DamageReceivedCount; // Controlled replicated-counter fixture; not a claim of actual GAS damage/network delivery.
        Session->Tick(0);SettleClose();
        if(!Check(!Session->GetView().IsSet()&&PC->GetViewTarget()==Before,TEXT("Changed damage serial interrupts and restores the production session")))return;
        if(!Open(*Registrar))return;
        ++Registrar->InteractionRevision;Session->Tick(0);SettleClose();
        if(!Check(!Session->GetView().IsSet()&&PC->GetViewTarget()==Before,TEXT("Target replacement revision revokes subtitle and camera")))return;
        if(!Open(*Registrar))return;
        auto* External=GetWorld()->SpawnActor<ACameraActor>();if(!Check(External!=nullptr,TEXT("External view fixture created")))return;
        PC->SetViewTarget(External);Session->Tick(0);
        if(!Check(!Session->GetView().IsSet()&&PC->GetViewTarget()==External&&LocalCameras()==Cameras,TEXT("A later camera owner is never overwritten")))return;
        PC->SetViewTarget(Before);
        if(!Open(*Registrar))return;
        TWeakObjectPtr<AAetherDialogueCameraActor> Outgoing=Cast<AAetherDialogueCameraActor>(PC->GetViewTarget());
        PC->SetViewTargetWithBlend(External,1.f,VTBlend_Linear,0.f,false);Session->Close();
        if(!Check(PC->PlayerCameraManager->PendingViewTarget.Target==External&&Outgoing.IsValid()&&!Outgoing->IsActorBeingDestroyed(),TEXT("Closing during external blend preserves its pending target and outgoing camera")))return;
        PC->PlayerCameraManager->UpdateCamera(2.f);if(Outgoing.IsValid())Outgoing->Tick(0);
        if(!Check(PC->GetViewTarget()==External&&(!Outgoing.IsValid()||Outgoing->IsActorBeingDestroyed()),TEXT("Surrendered camera retires only after engine current/pending references clear")))return;
        PC->SetViewTarget(Before);External->Destroy();
        AAetherFrontierProp* Board=nullptr;for(const auto& Candidate:Props)if(IsValid(Candidate)&&Candidate->Service==TEXT("Daily")){Board=Candidate.Get();break;}
        if(!Check(Board!=nullptr,TEXT("Production board instance is loaded")))return;
        C->SetActorLocation(Board->GetActorLocation()+FVector(-150,0,0));
        if(!Check(Open(*Board)&&PC->GetViewTarget()==Before&&LocalCameras()==Cameras,TEXT("Camera-null board opens without allocating a shot")))return;
        Session->Close();SettleClose();S.Stage=1;return;
    }
    if(S.Stage==1)
    {
        // Minimal prerequisite uses real trusted-fact transactions, never a direct profile write.
        if(Now<S.Next)return;S.Next=Now+.25;
        if(!P.Claims.Contains(TEXT("Q_Main_01")))
        {
            const auto* Quest=D.Rules.Quest(TEXT("Q_Main_01"));if(!Quest){Finish(false,TEXT("Missing prerequisite definition"));return;}
            for(FName Fact:Quest->Objectives)if(!P.Evidence.Contains(Fact.ToString()))
            {FAetherServerFact Event;Event.CharacterId=P.CharacterId;Event.FactId=Fact.ToString();FString Why;if(!Runtime->ObserveServerFact(MoveTemp(Event),Why))Finish(false,Why);return;}
            return;
        }
        if(Client->HasPending()||!Open(*Registrar))return;
        Session->Skip(Session->GetVersion());
        const auto* View=Session->GetView().IsSet()?&Session->GetView().GetValue():nullptr;
        const int32 Index=View?View->Choices.IndexOfByPredicate([](const auto& Choice){return Choice.ActionId==TEXT("Register")&&Choice.Availability==EAetherOfferAvailability::Available;}):INDEX_NONE;
        if(!Check(Index!=INDEX_NONE,TEXT("Production dialogue exposes current register service")))return;
        FString Why;if(!Session->Choose(Index,Session->GetVersion(),Why,&S.ExpectedCommandId)){Finish(false,Why);return;}
        Session->Close();SettleClose();
        if(!Check(S.ExpectedCommandId.IsValid()&&!Session->GetView().IsSet(),TEXT("Close after actual submission preserves its receipt identity")))return;
        S.Stage=2;return;
    }
    if(S.Stage==2)
    {
        if(!S.Reply.IsSet())return;
        if(S.Reply->Code==EAetherCommandCode::Busy||S.Reply->Code==EAetherCommandCode::StorageUnavailable)
        {S.Reply.Reset();if(Client->PresentationState()==EAetherCommandPresentation::Recovering)Finish(false,TEXT("Transient service failure exhausted retries"));return;}
        if(!Check(S.Reply->CommandId==S.ExpectedCommandId&&(S.Reply->Code==EAetherCommandCode::Applied||S.Reply->Code==EAetherCommandCode::Replayed),TEXT("The exact submitted service succeeds after dialogue closure")))return;
        if(Client->HasPending()||P.Revision<S.Reply->FinalProfileRevision||!P.bRegistered)return;
        const auto BeforeOpen=PC->GetViewTarget();if(!Open(*Registrar))return;
        Client->DetachController(PC);Session->Tick(0);SettleClose();
        if(!Check(!Session->GetView().IsSet()&&PC->GetViewTarget()==BeforeOpen,TEXT("Channel detach closes the actual owner-local session and releases camera")))return;
        Finish(true,TEXT("Production dialogue lifecycle and service-after-close checked; prerequisite/damage fixtures synthetic; no visual or splitscreen acceptance"));
    }
#endif
}
