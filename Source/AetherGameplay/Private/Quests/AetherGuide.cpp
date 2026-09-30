#include "Quests/AetherGuide.h"
#include "Definitions/AetherV10Definitions.h"
#include "World/AetherNativeContainer.h"
#include "Interaction/AetherNativeInteraction.h"
#include "Interaction/AetherWorldActionComponent.h"
#include "Framework/AetherFrontier.h"
#include "Definitions/AetherRules.h"
#include "Definitions/AetherWorldDefinition.h"
#include "Quests/AetherQuestProgression.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "ReactiveWorldSubsystem.h"
#include "EngineUtils.h"
namespace AetherGuide
{
FName SelectQuest(const FAetherProfileStateV10& P,FName Preferred,bool Cycle)
{
 const auto& Quests=FAetherRules::Get().Quests;const int32 Count=Quests.Num();if(!Count)return NAME_None;
 if(!Cycle&&AetherQuestProgression::Available(P,Preferred.ToString(),FAetherRules::Get()))return Preferred;
 const int32 Index=Quests.IndexOfByPredicate([&](const auto& Q){return Q.Id==Preferred;});
 const int32 Start=Cycle&&Index!=INDEX_NONE?(Index+1)%Count:0;
 for(int32 I=0;I<Count;++I){const FName Id=Quests[(Start+I)%Count].Id;if(AetherQuestProgression::Available(P,Id.ToString(),FAetherRules::Get()))return Id;}return NAME_None;
}
AAetherFrontierProp* SelectWaterReceiver(AAetherFrontierCharacter* C,AAetherFrontierProp* Container)
{
 if(!IsValid(C)||!IsValid(Container)||!C->HasAuthority()||!C->Alive())return nullptr;
 AAetherFrontierProp* Best=nullptr;double Distance=FMath::Square(FAetherRules::Get().PourRangeCm);
 for(TActorIterator<AAetherFrontierProp> It(C->GetWorld());It;++It)
 {
  auto* P=*It;if(P==Container||!P->bAcceptsWater||(P->Reactive->bOwnerOnlyStimuli&&P->GetOwner()!=C))continue;
  const double D=FVector::DistSquared(P->GetActorLocation(),Container->GetActorLocation());if(D>=Distance)continue;
  FCollisionQueryParams Params(SCENE_QUERY_STAT(ContainerReceiver),false,Container);Params.AddIgnoredActor(P);Params.AddIgnoredActor(C);
  if(C->GetWorld()->LineTraceTestByChannel(Container->GetActorLocation(),P->GetActorLocation(),ECC_Visibility,Params))continue;
  Best=P;Distance=D;
 }
 return Best;
}
FString ObjectiveLabel(FName Id){const auto* O=FAetherRules::Get().Objectives.Find(Id);return O?O->Label:Id.ToString();}
bool IsPersonalFire(FName Service){if(Service==FAetherRules::Get().PersonalTraining.FireObjectiveId&&!Service.IsNone())return true;for(const auto& D:FAetherRules::Get().Dailies)if(D.bPersonalFires&&D.Facts.Contains(Service))return true;return false;}
bool CanInspectFire(const AAetherFrontierProp* Fire)
{
 if(!IsValid(Fire)||!Fire->HasAuthority()||!Fire->Reactive)return false;
 const auto* Sim=Fire->GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation();
 const auto* State=Sim?Sim->Find(Fire->Reactive->GetBodyId()):nullptr;
 // Consumed fuel proves this is an existing cleared site, not the cold frame before initial ignition.
 return State&&!State->bBurning&&State->TemperatureC<Fire->Reactive->GetMaterial().IgnitionC&&State->FuelKg<Fire->Reactive->GetMaterial().InitialFuelKg-1.e-8;
}
FAetherGuidance Resolve(AAetherFrontierCharacter* C,const FAetherProfileStateV10* Snapshot)
{
 FAetherGuidance G;const auto& D=FAetherV10Definitions::Get();const auto& Definition=D.Guidance;
 if(!D.bValid){G.Title=TEXT("任务定义不可用");G.Hint=D.Error;return G;}
 G.Title=Definition.LoadingTitle;G.Hint=Definition.LoadingHint;
 if(!IsValid(C)||C->IsActorBeingDestroyed()||!Snapshot)return G;
 // A caller supplies its own current snapshot. Never substitute the listen server's newer profile.
 const auto& P=*Snapshot;G.bReady=true;G.ProfileRevision=P.Revision;
 G.Quest=SelectQuest(P,C->TrackedQuest);C->TrackedQuest=G.Quest;
 const auto Finish=[&]()
 {if(!P.PendingRewards.IsEmpty())G.Hint+=(G.Hint.IsEmpty()?FString():FString(LINE_TERMINATOR))+Definition.PendingRewardHint;return G;};
 const auto Anchor=[&](const FString& Id)
 {
  const auto* Placement=FAetherWorldDefinitions::Get().Find(FName(*Id));if(!Placement)return false;
  G.Position=Placement->Location;G.bHasTarget=true;
  for(TActorIterator<AAetherFrontierProp> It(C->GetWorld());It;++It)
   if(It->Spec.Id.ToString().Equals(Id,ESearchCase::CaseSensitive)&&It->bEnabled&&!It->IsActorBeingDestroyed())
   {G.Position=It->GetActorLocation();break;}
  return true;
 };
 if(G.Quest.IsNone())
 {
  bool Complete=!D.Rules.Quests.IsEmpty();for(const auto& Q:D.Rules.Quests)Complete&=P.Claims.Contains(Q.Id.ToString());
  const auto* A=FAetherWorldDefinitions::Get().Find(FName(*Definition.Completion.AnchorId));bool DailyUnlocked=false;
  if(A)for(const auto& Daily:D.Rules.Dailies)DailyUnlocked|=Daily.Service==A->Service&&(Daily.QuestGate.IsNone()||P.Claims.Contains(Daily.QuestGate.ToString()));
  if(Complete&&DailyUnlocked){G.Title=Definition.Completion.Title;G.Label=Definition.Completion.Label;G.Hint=Definition.Completion.Hint;Anchor(Definition.Completion.AnchorId);}
  return Finish();
 }
 const auto* Quest=D.Rules.Quest(G.Quest);if(!Quest)return G;
 G.Title=Quest->Title;G.Hint.Reset();G.bRewardReady=AetherQuestProgression::Complete(P,Quest->Id.ToString(),D.Rules);
 if(G.bRewardReady){G.Label=Definition.RewardReadyLabel;G.Hint=Definition.RewardReadyHint;return Finish();}
 for(FName Id:Quest->Objectives)if(!P.Evidence.Contains(Id.ToString())){G.Objective=Id;break;}
 const auto* Objective=D.Rules.Objectives.Find(G.Objective);if(!Objective)return Finish();
 G.Label=Objective->Label;G.Hint=Objective->Hint;G.Position=Objective->Position;G.bHasTarget=true;
 if(!Objective->Anchor.IsNone())Anchor(Objective->Anchor.ToString());
 const auto DynamicActor=[&](const FString& ObjectiveId)->AActor*
 {
  const auto* Rule=Definition.DynamicObjectives.Find(ObjectiveId);if(!Rule)return nullptr;
  if(Rule->Kind==EAetherGuidanceTargetKind::OwnedService)
  {
   for(TActorIterator<AAetherFrontierProp> It(C->GetWorld());It;++It)
    if(It->GetOwner()==C&&It->bEnabled&&!It->IsActorBeingDestroyed()&&It->Service.ToString().Equals(Rule->SelectorId,ESearchCase::CaseSensitive))return *It;
  }
  else for(TActorIterator<AAetherFrontierCharacter> It(C->GetWorld());It;++It)
   if(It->GetOwner()==C&&It->Alive()&&!It->IsActorBeingDestroyed()&&
      StaticEnum<EAetherFighter>()->GetNameStringByValue(int64(It->Fighter)).Equals(Rule->SelectorId,ESearchCase::CaseSensitive))return *It;
  return nullptr;
 };
 for(const auto& Preparation:Definition.Preparations)if(Preparation.QuestId.Equals(Quest->Id.ToString(),ESearchCase::CaseSensitive))
 {
  bool Needed=false;
  for(const auto& Skill:Preparation.RequiredPermanentSkills)Needed|=P.Skills.PermanentRank(Skill)<=0;
  for(const auto& Id:Preparation.RequiredLiveObjectives)Needed|=!P.Evidence.Contains(Id)&&!DynamicActor(Id);
  if(Needed){G.Label=Preparation.Label;G.Hint=Preparation.Hint;Anchor(Preparation.AnchorId);return Finish();}
 }
 if(auto* Target=DynamicActor(G.Objective.ToString()))G.Position=Target->GetActorLocation();
 for(const auto& Phase:Definition.EncounterPhases)
 {
  const auto* Reward=D.Rules.ActivityRewards.Find(FName(*Phase.EncounterId));if(!Reward||Reward->Objective!=G.Objective)continue;
  for(TActorIterator<AAetherEncounterDirector> It(C->GetWorld());It;++It)for(const auto* Run:{&It->Abbey,&It->Relay})
   if(Run->Definition.ToString().Equals(Phase.EncounterId,ESearchCase::CaseSensitive)&&Run->Participants.Contains(P.CharacterId)&&
      StaticEnum<EAetherEncounterPhase>()->GetNameStringByValue(int64(Run->Phase)).Equals(Phase.Phase,ESearchCase::CaseSensitive))
   {
    G.Label=Phase.Label;G.Hint=Phase.Hint;
    if(!Phase.AnchorId.IsEmpty())Anchor(Phase.AnchorId);
    else if(const auto* Encounter=D.Rules.Encounters.Find(FName(*Phase.EncounterDefinitionId)))G.Position=Encounter->Center;
    return Finish();
   }
 }
 return Finish();
}

FAetherInteractionTarget QueryTarget(AAetherFrontierCharacter* C,AActor* Actor)
{
 FAetherInteractionTarget R;
 if(!IsValid(C)||!IsValid(Actor)||!C->Alive()||C->bTravelPending||Actor==C||Actor->GetWorld()!=C->GetWorld())return R;
 const auto* Registry=C->GetWorld()->GetSubsystem<UAetherNearbyRegistry>();
 if(!Registry||!Registry->Contains(Actor))return R;
 auto* Target=Cast<AAetherFrontierProp>(Actor);auto* Downed=Cast<AAetherFrontierCharacter>(Actor);
 const bool Rescue=Downed&&Downed->Fighter==EAetherFighter::Player&&!Downed->Alive();
 auto* Container=(C->SkillAuthority==EAetherSkillAuthority::Profile)?Cast<AAetherNativeContainer>(Actor):nullptr;
 if(!Target&&!Rescue&&!Container)return R;
 if(Target&&(!Target->bEnabled||Target->Service.IsNone()||((IsPersonalFire(Target->Service)||Target->Reactive->bOwnerOnlyStimuli)&&Target->GetOwner()!=C)))return R;
 const double Radius=Rescue?200.:250.;
 if(FVector::DistSquared(C->GetActorLocation(),Actor->GetActorLocation())>FMath::Square(Radius))return R;
 FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherInteraction),false,C);Q.AddIgnoredActor(Actor);
 if(C->GetWorld()->LineTraceTestByChannel(C->GetActorLocation(),Actor->GetActorLocation(),ECC_Visibility,Q))return R;
 R.ProfileRevision=C->ProfileState()?C->ProfileState()->Profile.Revision:-1;
 const FString Key=TEXT("[")+C->BindingFor("Interact").GetDisplayName().ToString()+TEXT("] ");
 if(Container)
 {
  if(Container->StableId.IsEmpty()||Container->Revision<0)return R;
  R.Container=Container;R.StableId=FName(*Container->StableId);R.ActionId="OpenContainer";R.bExecutable=true;
  R.Prompt=Key+(Container->ContainerKind==uint8(EAetherContainerKind::WorldDrop)?TEXT("查看掉落"):Container->ContainerKind==uint8(EAetherContainerKind::PersonalStorage)?TEXT("打开个人仓储"):TEXT("打开共享箱子"));return R;
 }
 if(Rescue){R.Rescue=Downed;R.ActionId="Revive";R.bExecutable=true;R.Prompt=Key+TEXT("救援队友：保持靠近 3 秒");return R;}
 R.Prop=Target;R.StableId=Target->Spec.Id;R.ActionId=Target->Service;const FName S=Target->Service;
 if((C->SkillAuthority==EAetherSkillAuthority::Profile))if(auto Native=AetherNativeInteraction::Provider(*C,*Target);Native.IsSet())
 {
  const auto Offers=Native->Query({C->ProfileState()->Profile.CharacterId,Target->Spec.Id.ToString()});
  for(const auto& Offer:Offers)if(Offer.bPreferred)
  {R.ActionId=FName(*Offer.ActionId);R.bExecutable=true;R.Prompt=Key+Offer.DisplayVerb;return R;}
  if(!Offers.IsEmpty()){R.Prompt=AetherInteractionQueries::ReasonText(Offers[0].ReasonId,Offers[0].ReasonParameters,FAetherV10Definitions::Get().Rules);return R;}
  if(!Target->bCarryable)return {};
 }
 static const TMap<FName,FString> Labels={{"SupplyA",TEXT("拾取补给")},{"SupplyB",TEXT("拾取补给")},{"Gate",TEXT("进入城镇")},{"Register",TEXT("与登记员交谈")},{"Inn",TEXT("绑定据点 / 休息")},{"Teacher",TEXT("学习能力 / 开始个人训练")},{"Shop",TEXT("与补给商人交易")},{"Recruit",TEXT("招募同行者")},{"SealDelivered",TEXT("交付古印")},{"Daily",TEXT("结算补给委托")},{"DailyPatrol",TEXT("查看 / 结算巡逻委托")},{"DailyFire",TEXT("领取 / 结算灭火委托")},{"Well",TEXT("从水井补充有限储水")},{"Bucket",TEXT("倾倒水桶 / 检查已清理现场")},{"HingedGate",TEXT("切换门机关")},{"Source",TEXT("切换固定电源")},{"SupplyRestored",TEXT("操作机械水泵")},{"Receiver",TEXT("检查电动水泵")},{"Rescue",TEXT("救援工匠")},{"Abbey",TEXT("进入修道院挑战")},{"AbbeyValve",TEXT("引导阀门")},{"GuardianDefeated",TEXT("领取遭遇凭据")},{"Activity",TEXT("开始 / 引导中继防守")},{"Gather",TEXT("采集补给")},{"Loot",TEXT("拾取共享掉落")}};
 if(IsPersonalFire(S))R.Prompt=TEXT("瞄准个人火盆使用引泉，实际灭火才会完成目标");
 else if(Target->bInspectableFire){R.bExecutable=!Target->Reactive->State.bBurning;R.Prompt=R.bExecutable?Key+TEXT("检查清理后的火点"):TEXT("使用引泉或旁边水桶灭火");}
 else if(S=="Dummy")R.Prompt=TEXT("装备训练剑，轻击训练木桩");
 else if(Target->bCarryable)
 {
  const FString Why=UAetherWorldActionComponent::ManipulationReason(*C,*Target);
  R.Prompt=Why.IsEmpty()?TEXT("[")+C->BindingFor("Carry").GetDisplayName().ToString()+TEXT("] 瞄准搬起 · [")+C->BindingFor("Push").GetDisplayName().ToString()+TEXT("] 推动 · 搬起后可投掷"):Why;
  R.Prompt+=TEXT(" · 世界物件，不能直接收入背包");
 }
 else if(const auto* Label=Labels.Find(S)){R.Prompt=Key+*Label;R.bExecutable=true;}
 else if(FAetherV10Definitions::Get().Economy.Shops.Contains(S.ToString())){R.Prompt=Key+TEXT("与商人交易");R.bExecutable=true;}
 else if(S.ToString().StartsWith("Patrol")){R.Prompt=Key+TEXT("记录巡逻位置");R.bExecutable=true;}
 else if(S=="Support")R.Prompt=TEXT("用训练剑切断，或用引焰烧毁支撑绳索");
 else if(S=="Water")R.Prompt=TEXT("对浅水释放霜凝，可形成承重冰面");
 if(R.Prompt.IsEmpty())return {}; // 无已定义交互/物理提示的背景对象不抢占焦点。
 if(Target->bWorkshopService)R.Prompt+=TEXT(" · 修复练习工坊（电路或手动泵均可）");
 if(S=="Inn"||S=="Well"||S=="Bucket")R.Prompt+=TEXT(" · 缺水可到旅店休息补充施法储水");
 const auto& State=Target->Reactive->State;
 if(State.bBroken)R.Prompt+=TEXT(" · 已断裂");
 else if(State.bBurning)R.Prompt+=TEXT(" · 正在燃烧");
 else if(Target->bExtinguished)R.Prompt+=TEXT(" · 已熄灭");
 if(Target->Reactive->IceSupport==EReactiveIceSupport::FreezePending)R.Prompt+=TEXT(" · 冻结等待，请离开水面");
 if(Target->Reactive->IceSupport==EReactiveIceSupport::Thawing)R.Prompt+=TEXT(" · 冰面融化，立即撤离");
 if(Target->ReceivedPower>1)R.Prompt+=TEXT(" · 已通电");
 else if(Target->Reactive->bElectricalContact)R.Prompt+=TEXT(" · 已接触导体，尚未获得有效功率");
 return R;
}
bool ValidateSelection(AAetherFrontierCharacter* C,const FAetherInteractionTarget& S)
{
 if(!IsValid(C)||!C->ProfileState()||S.ProfileRevision!=C->ProfileState()->Profile.Revision)return false;
 if(S.Container.IsValid())
 {
  const auto Current=QueryTarget(C,S.Container.Get());
  return Current.Container==S.Container&&Current.bExecutable&&Current.StableId.ToString().Equals(S.StableId.ToString(),ESearchCase::CaseSensitive);
 }
 if(S.Prop.IsValid()==S.Rescue.IsValid())return false;
 auto Current=QueryTarget(C,S.Prop.IsValid()?static_cast<AActor*>(S.Prop.Get()):static_cast<AActor*>(S.Rescue.Get()));
 // 用完整拼写比较，而不是 FName 的大小写折叠比较；缓存动作变化也会使旧选择失效。
 return Current.bExecutable&&Current.Prop==S.Prop&&Current.Rescue==S.Rescue&&
     Current.StableId.ToString().Equals(S.StableId.ToString(),ESearchCase::CaseSensitive)&&
     Current.ActionId.ToString().Equals(S.ActionId.ToString(),ESearchCase::CaseSensitive);
}
FAetherInteractionTarget SelectInteraction(AAetherFrontierCharacter* C,AActor* Previous)
{
 FAetherInteractionTarget Best;if(!IsValid(C)||!C->Alive())return Best;
 const auto* Registry=C->GetWorld()->GetSubsystem<UAetherNearbyRegistry>();if(!Registry)return Best;
 double BestScore=-DBL_MAX;FString BestKey;
 for(const auto& Weak:Registry->Nearby(C->GetActorLocation(),250.))
 {
     auto* Actor=Weak.Get();auto Candidate=QueryTarget(C,Actor);if(Candidate.Prompt.IsEmpty())continue;
     const FVector Delta=Actor->GetActorLocation()-C->GetActorLocation();
     // 视角中心占主要权重，距离作连续修正；救援优先于常规服务。
     // 当前焦点获得小幅优势，只有真实更优或旧目标失效时才切换，避免边界抖动。
     double Score=FVector::DotProduct(C->GetControlRotation().Vector(),Delta.GetSafeNormal())-.25*Delta.Size()/250.;
     if(Candidate.Rescue.IsValid())Score+=3.;
     if(Candidate.bExecutable)Score+=.1;
     if(Actor==Previous)Score+=.18;
     const FString Key=Candidate.StableId.ToString()+Actor->GetName();
     if(Score>BestScore||(Score==BestScore&&Key.Compare(BestKey,ESearchCase::CaseSensitive)<0))
     {BestScore=Score;BestKey=Key;Best=MoveTemp(Candidate);}
 }
 return Best;
}

}
