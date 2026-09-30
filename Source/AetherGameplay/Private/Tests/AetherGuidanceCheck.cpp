#include "Framework/AetherFrontier.h"
#include "Framework/AetherPlayerController.h"
#include "Quests/AetherGuide.h"
#include "Definitions/AetherV10Definitions.h"
#include "Networking/AetherCommandClient.h"
#include "Networking/AetherCommandRuntime.h"
#include "Interaction/AetherNativeInteraction.h"
#include "Commands/AetherServerFactCoordinator.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Controller.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

#if !UE_BUILD_SHIPPING
namespace
{
struct FGuideCheckState
{
 TWeakObjectPtr<AAetherFrontierMode> Mode;
 TWeakObjectPtr<UAetherCommandClient> Client;
 FDelegateHandle ReplyHandle;
 int32 Stage=0;
 double Started=0,StageAt=0,NextAction=0;
 bool Initialized=false,AwaitingReply=false,Done=false;
 FGuid Channel,ExpectedCommandId;
 TOptional<FAetherCommandResult> Reply;
};
FGuideCheckState CheckState;
}
#endif
void AAetherFrontierMode::CheckGuidance()
{
#if !UE_BUILD_SHIPPING
 auto& S=CheckState;const double Now=FPlatformTime::Seconds();
 if(S.Mode.Get()!=this)
 {
  if(S.Client.IsValid())S.Client->OnResult.Remove(S.ReplyHandle);
  S={};S.Mode=this;S.Started=S.StageAt=Now;
 }
 if(S.Done)return;
 const auto Finish=[&](bool Passed,const FString& Reason)
 {
  S.Done=true;if(S.Client.IsValid())S.Client->OnResult.Remove(S.ReplyHandle);
  UE_LOG(LogTemp,Display,TEXT("AETHER_GUIDANCE_%s stage=%d scope=current_snapshot_service synthetic_setup=true reason=%s"),Passed?TEXT("PASS"):TEXT("FAIL"),S.Stage,*Reason);
  FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
 };
 const auto Check=[&](bool Passed,const TCHAR* Name)
 {UE_LOG(LogTemp,Display,TEXT("GUIDANCE_CHECK %s %s"),Passed?TEXT("PASS"):TEXT("FAIL"),Name);if(!Passed)Finish(false,Name);return Passed;};
 const auto Next=[&](int32 Stage){S.Stage=Stage;S.StageAt=Now;};
 if(Now-S.Started>180||Now-S.StageAt>60){Finish(false,TEXT("Current snapshot/operation stage timed out"));return;}
 auto* C=Cast<AAetherFrontierCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
 auto* PC=C?Cast<AAetherPlayerController>(C->GetController()):nullptr;auto* LP=PC?PC->GetLocalPlayer():nullptr;
 auto* Client=LP?LP->GetSubsystem<UAetherCommandClient>():nullptr;
 auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>();
 if(!C||!PC||!PC->HasAuthority()||!Client||!Runtime||!Runtime->HasBackend()||!Client->GetChannel().IsValid()||!Client->GetProfile().IsSet())return;
 const auto& P=Client->GetProfile().GetValue();const auto& D=FAetherV10Definitions::Get();
 if(!S.Initialized)
 {
  FGuid PrefixId;const FString Prefix=TEXT("AetherGuideCheck_");
  if(!Check(SavePrefix.StartsWith(Prefix,ESearchCase::CaseSensitive)&&FGuid::ParseExact(SavePrefix.Mid(Prefix.Len()),EGuidFormats::Digits,PrefixId)&&
      P.Revision==0&&P.Claims.IsEmpty()&&P.Evidence.IsEmpty(),TEXT("Synthetic setup requires a fresh isolated current-schema namespace")))return;
  S.Initialized=true;S.Client=Client;S.Channel=Client->GetChannel();
  S.ReplyHandle=Client->OnResult.AddWeakLambda(this,[](const FAetherCommandResult& R)
  {
   auto& State=CheckState;
   if(State.AwaitingReply&&State.ExpectedCommandId.IsValid()&&State.Client.IsValid()&&
      State.Client->GetChannel()==State.Channel&&R.CommandId==State.ExpectedCommandId)State.Reply=R;
  });
  FAetherProfileStateV10 Fixture;
  if(!Check(AetherGuide::SelectQuest(Fixture,TEXT("Q_Main_08"))==TEXT("Q_Main_01"),TEXT("Locked preference selects first current available quest")))return;
  Fixture.Claims={TEXT("Q_Main_01"),TEXT("Q_Main_02"),TEXT("Q_Main_03")};
  if(!Check(AetherGuide::SelectQuest(Fixture,TEXT("Q_Main_04"),true)==TEXT("Q_Main_05")&&AetherGuide::SelectQuest(Fixture,TEXT("Q_Main_05"),true)==TEXT("Q_Main_04"),TEXT("Independent field quests cycle")))return;
  Fixture.Claims.Add(TEXT("Q_Main_04"));if(!Check(AetherGuide::SelectQuest(Fixture,TEXT("Q_Main_04"))==TEXT("Q_Main_05"),TEXT("Claimed branch advances")))return;
  Fixture.Claims.Add(TEXT("Q_Main_05"));if(!Check(AetherGuide::SelectQuest(Fixture,TEXT("Q_Main_04"))==TEXT("Q_Main_06"),TEXT("Recruit requires both branches")))return;
  Next(1);
 }
 if(Client!=S.Client.Get()||Client->GetChannel()!=S.Channel){Finish(false,TEXT("Probe actor/channel changed; no cross-owner continuation"));return;}
 if(!C->Alive()){Finish(false,TEXT("Probe character downed"));return;}
 Client->RequestSnapshot();
 if(S.Stage==1)
 {
  // Synthetic prerequisites use the real trusted-fact transaction service; never write PS->Profile or publish a fabricated view.
  if(Now<S.NextAction)return;S.NextAction=Now+.25;
  for(const TCHAR* Id:{TEXT("Q_Main_01"),TEXT("Q_Main_02"),TEXT("Q_Main_03")})
  {
   if(P.Claims.Contains(Id))continue;const auto* Quest=D.Rules.Quest(FName(Id));
   if(!Quest){Finish(false,TEXT("Synthetic prerequisite definition missing"));return;}
   for(FName Objective:Quest->Objectives)if(!P.Evidence.Contains(Objective.ToString()))
   {
    FAetherServerFact Event;Event.CharacterId=P.CharacterId;Event.FactId=Objective.ToString();FString Why;
    if(!Runtime->ObserveServerFact(MoveTemp(Event),Why))Finish(false,Why);
    return; // Await the published client snapshot before choosing the next fact.
   }
   return; // Auto-claim is part of the same transaction, not a local fixture mutation.
  }
  C->TrackedQuest=TEXT("Q_Main_04");const auto Guide=AetherGuide::Resolve(C,&P);
  if(!Check(Guide.Objective==TEXT("ForestFire0")&&Guide.bHasTarget&&Guide.ProfileRevision==P.Revision,TEXT("Current committed snapshot retains persistent forest guidance")))return;
  C->SetActorLocation(D.Rules.Objectives.FindChecked(TEXT("ForestFire0")).Position+FVector(0,-160,40));UpdateRegions({C->GetActorLocation()});Next(2);return;
 }
 const auto SubmitObserve=[&](AAetherFrontierProp* Target)
 {
  if(!Target||!C->Ready()||Client->HasPending())return false;
  const auto* Server=C->ProfileState()?C->ProfileState()->GetNativeProfile():nullptr;
  if(!Server||Server->Revision!=P.Revision)return false;
  auto Provider=AetherNativeInteraction::Provider(*C,*Target);if(!Provider.IsSet())return false;
  const auto* Definition=D.Interactions.Targets.Find(Target->Service.ToString());if(!Definition)return false;
  const auto* Action=Definition->Actions.FindByPredicate([](const auto& A){return A.Kind==EAetherInteractionActionKind::ObserveObjective;});if(!Action)return false;
  const auto Offers=Provider->Query({Client->GetOwnerIdentity(),Target->Spec.Id.ToString()});
  const auto* Offer=Offers.FindByPredicate([&](const auto& O){return O.ActionId==Action->Id&&O.Availability==EAetherOfferAvailability::Available;});if(!Offer)return false;
  FAetherInteractionSelection Selection{Offer->TargetStableId,Offer->ActionId,Offer->ProfileRevision,Offer->WorldRevision,Offer->TargetRevision};
  S.AwaitingReply=true;S.Reply.Reset();FString Why;
  if(!AetherNativeInteraction::Submit(*C,Selection,Why,&S.ExpectedCommandId)){S.AwaitingReply=false;S.Reply.Reset();return false;}
  return Check(S.ExpectedCommandId.IsValid(),TEXT("Persistent interaction exposes the submitted receipt identity"));
 };
 const auto AwaitTransientRetry=[&]()
 {
  if(!S.Reply.IsSet())return false;
  if(S.Reply->Code!=EAetherCommandCode::Busy&&S.Reply->Code!=EAetherCommandCode::StorageUnavailable)return false;
  // Consume this transient reply once; the production queue retries the same frozen command, never a fresh intent.
  S.Reply.Reset();
  if(!Client->HasPending()||Client->PresentationState()==EAetherCommandPresentation::Recovering)
   Finish(false,TEXT("Native interaction transient response exhausted the client retry budget"));
  return true; // Missing final reply remains bounded by the stage/global timeout above.
 };
 if(S.Stage==2)
 {
  auto* Fire=Prop(TEXT("ForestFire0"));if(!Fire||!Prop(TEXT("ForestFire1"))||!Prop(TEXT("ForestFire2"))||!Prop(TEXT("Rescue")))return;
  if(!C->Ready()||!Fire->bEnabled)return;
  C->SetActorLocation(Fire->GetActorLocation()+FVector(0,-160,40));PC->SetControlRotation((Fire->GetActorLocation()-C->GetActorLocation()).Rotation());
  const auto Guide=AetherGuide::Resolve(C,&P);
  if(!Check(Guide.Position.Equals(Fire->GetActorLocation()),TEXT("Loaded objective uses actual actor position"))||
     !Check(AetherGuide::SelectInteraction(C).Prop==Fire,TEXT("Displayed and submitted interaction selects the same nearby instance"))||
     !Check(Fire->Reactive->State.bBurning,TEXT("Negative interaction begins with an actually burning fire")))return;
  if(SubmitObserve(Fire))Next(3);return;
 }
 if(S.Stage==3)
 {
  if(AwaitTransientRetry()||!S.Reply.IsSet())return;
  if(!Check(S.Reply->CommandId==S.ExpectedCommandId,TEXT("Negative fire receipt matches submitted command")))return;
  const auto Code=S.Reply->Code;
  S.AwaitingReply=false;
  if(Code==EAetherCommandCode::StaleRevision){Next(2);return;}
  if(!Check(Code==EAetherCommandCode::NotReady&&!P.Evidence.Contains(TEXT("ForestFire0")),TEXT("Native server rejects inspection as a bypass for a burning fire")))return;
  Next(4);return;
 }
 if(S.Stage==4)
 {
  const auto* Board=Prop(TEXT("FireBoard"));
  if(!Check(Board&&!Board->Reactive->bOwnerOnlyStimuli&&Board->Reactive->StableId==TEXT("FireBoard"),TEXT("Commission board remains public and persistent")))return;
  const FVector TestLocation(-2000,-2000,50);C->SetActorLocation(TestLocation+FVector(0,-150,50));PC->SetControlRotation((TestLocation-C->GetActorLocation()).Rotation());
  auto* Own=Make(TEXT("GuideTestFire"),TEXT("TrainingExtinguished"),TestLocation,FVector(.6),EAetherObjectKind::Timber,TEXT(""));
  if(!Check(Own!=nullptr,TEXT("Private target fixture created")))return;Own->SetOwner(C);
  if(!Check(AetherGuide::SelectInteraction(C).Prop==Own,TEXT("Personal target visible to its owner")))return;
  Own->SetOwner(C->ProfileState());if(!Check(AetherGuide::SelectInteraction(C).Prop!=Own,TEXT("Other-owner private target excluded")))return;
  Own->Destroy();Props.Remove(Own);
  C->SetActorLocation(Prop(TEXT("ForestFire0"))->GetActorLocation()+FVector(0,-160,40));
  for(int32 I=0;I<3;++I){FReactiveStimulus Water;Water.SourceActor=C;Water.WaterKg=1;Prop(FName(*FString::Printf(TEXT("ForestFire%d"),I)))->Reactive->Inject(Water);}
  Next(5);return;
 }
 if(S.Stage==5)
 {
  bool Cleared=true,Published=true;
  for(int32 I=0;I<3;++I){const FString Id=FString::Printf(TEXT("ForestFire%d"),I);Cleared&=AetherGuide::CanInspectFire(Prop(FName(*Id)));Published&=P.Evidence.Contains(Id);}
  if(!Cleared||!Published)return;
  if(!Check(Cleared&&Published,TEXT("Real water reaction credits all sites through committed client snapshots")))return;
  auto* Rescue=Prop(TEXT("Rescue"));if(!Rescue)return;
  C->SetActorLocation(Rescue->GetActorLocation()+FVector(0,-160,40));PC->SetControlRotation((Rescue->GetActorLocation()-C->GetActorLocation()).Rotation());Next(6);return;
 }
 if(S.Stage==6){if(SubmitObserve(Prop(TEXT("Rescue"))))Next(7);return;}
 if(S.Stage==7)
 {
  if(AwaitTransientRetry()||!S.Reply.IsSet())return;
  if(!Check(S.Reply->CommandId==S.ExpectedCommandId,TEXT("Rescue receipt matches submitted command")))return;
  const auto Code=S.Reply->Code;
  if(Code==EAetherCommandCode::StaleRevision){S.AwaitingReply=false;Next(6);return;}
  if(!Check(Code==EAetherCommandCode::Applied||Code==EAetherCommandCode::Replayed,TEXT("Rescue uses the native persistent interaction service")))return;
  if(Client->HasPending()||P.Revision<S.Reply->FinalProfileRevision||!P.Claims.Contains(TEXT("Q_Main_04")))return;
  S.AwaitingReply=false;const auto Guide=AetherGuide::Resolve(C,&P);
  if(!Check(Guide.Quest==TEXT("Q_Main_05")&&Guide.Objective==TEXT("SupplyRestored")&&Guide.ProfileRevision==P.Revision,TEXT("Guidance advances only after the client snapshot commit barrier")))return;
  Finish(true,TEXT("Current snapshot guidance, native rejection/rescue and real reaction assertions complete; setup was synthetic, not a full mainline/UI journey"));
 }
#endif
}
