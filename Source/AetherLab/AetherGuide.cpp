#include "AetherGuide.h"
#include "AetherFrontier.h"
#include "AetherRules.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "ReactiveWorldSubsystem.h"
#include "EngineUtils.h"
namespace AetherGuide
{
FName SelectQuest(const FAetherProfile& P,FName Preferred,bool Cycle)
{
 const auto& Quests=FAetherRules::Get().Quests;const int32 Count=Quests.Num();if(!Count)return NAME_None;
 if(!Cycle&&P.Available(Preferred))return Preferred;
 const int32 Index=Quests.IndexOfByPredicate([&](const auto& Q){return Q.Id==Preferred;});
 const int32 Start=Cycle&&Index!=INDEX_NONE?(Index+1)%Count:0;
 for(int32 I=0;I<Count;++I){const FName Id=Quests[(Start+I)%Count].Id;if(P.Available(Id))return Id;}return NAME_None;
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
bool IsPersonalFire(FName Service){if(Service=="TrainingExtinguished")return true;for(const auto& D:FAetherRules::Get().Dailies)if(D.bPersonalFires&&D.Facts.Contains(Service))return true;return false;}
bool CanInspectFire(const AAetherFrontierProp* Fire)
{
 if(!IsValid(Fire)||!Fire->HasAuthority()||!Fire->Reactive)return false;
 const auto* Sim=Fire->GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation();
 const auto* State=Sim?Sim->Find(Fire->Reactive->GetBodyId()):nullptr;
 // Consumed fuel proves this is an existing cleared site, not the cold frame before initial ignition.
 return State&&!State->bBurning&&State->TemperatureC<Fire->Reactive->GetMaterial().IgnitionC&&State->FuelKg<Fire->Reactive->GetMaterial().InitialFuelKg-1.e-8;
}
FAetherGuidance Resolve(AAetherFrontierCharacter* C)
{
 FAetherGuidance G;if(!C||!C->ProfileState())return G;
 const auto& P=C->ProfileState()->Profile;G.Quest=SelectQuest(P,C->TrackedQuest);C->TrackedQuest=G.Quest;
 if(G.Quest.IsNone()){G.Title=TEXT("主线已完成");G.Label=TEXT("镇上委托与中继防守已开放");G.Hint=TEXT("到公告板领取每日委托。");G.Position=FVector(-900,300,100);G.bHasTarget=P.Claims.Contains(FName("Q_Main_08"));return G;}
 G.Title=FAetherProfile::QuestTitle(G.Quest);G.bRewardReady=P.Complete(G.Quest);
 if(G.bRewardReady){G.Label=TEXT("目标完成，奖励待领");G.Hint=TEXT("整理背包后，在任务面板领取奖励。");return G;}
 for(auto Id:FAetherProfile::Objectives(G.Quest))if(!P.Evidence.Contains(Id)){G.Objective=Id;break;}
 const auto* O=FAetherRules::Get().Objectives.Find(G.Objective);if(!O)return G;
 G.Label=O->Label;G.Hint=O->Hint;G.Position=O->Position;G.bHasTarget=true;FName Anchor=O->Anchor;
 auto Locate=[&](FName Id)->AAetherFrontierProp*{for(TActorIterator<AAetherFrontierProp> It(C->GetWorld());It;++It)if(It->Spec.Id==Id)return *It;return nullptr;};
 if(G.Quest=="Q_Main_03")
 {
  const FName PersonalId=*FString(TEXT("Training_")+P.CharacterId);auto* Fire=Locate(PersonalId);AAetherFrontierCharacter* Trainer=nullptr;
  for(TActorIterator<AAetherFrontierCharacter> It(C->GetWorld());It;++It)if(It->Fighter==EAetherFighter::ShieldGuard&&It->Alive()&&It->GetOwner()==C){Trainer=*It;break;}
  const bool NeedTeacher=(P.LearnedSpells&3)!=3||(!P.Evidence.Contains("TrainingExtinguished")&&!Fire)||(!P.Evidence.Contains("Block")&&!Trainer);
  if(NeedTeacher){Anchor="Teacher";G.Position=FVector(400,-300,90);G.Label=TEXT("拜访导师，准备个人训练");G.Hint=TEXT("与导师交谈，学习引焰 / 引泉并生成训练目标。");}
  else if(G.Objective=="TrainingExtinguished"&&Fire){Anchor=PersonalId;G.Position=Fire->GetActorLocation();}
  else if(G.Objective=="Block"&&Trainer){Anchor=NAME_None;G.Position=Trainer->GetActorLocation();}
 }
 if(G.Quest=="Q_Main_07")for(TActorIterator<AAetherEncounterDirector> It(C->GetWorld());It;++It)if(It->Abbey.Participants.Contains(P.CharacterId))
 {
  switch(It->Abbey.Phase)
  {
   case EAetherEncounterPhase::Front:Anchor=NAME_None;G.Position=FVector(0,25000,120);G.Label=TEXT("击败前庭守卫");G.Hint=TEXT("与同行者清理前庭，保护自己的生命与体力。");break;
   case EAetherEncounterPhase::Channel:Anchor="AbbeyValve";G.Position=FVector(0,25500,90);G.Label=TEXT("保护并引导水道阀门");G.Hint=TEXT("在阀门旁交互，或指挥同行者引导。");break;
   case EAetherEncounterPhase::Elite:Anchor=NAME_None;G.Position=FVector(0,26600,120);G.Label=TEXT("清除侧廊精英");break;
   case EAetherEncounterPhase::Boss:Anchor=NAME_None;G.Position=FVector(0,27400,120);G.Label=TEXT("击败铸钟守卫");G.Hint=TEXT("破坏架势，过热时用水，抓住暴露窗口。");break;
   default:break;
  }
 }
 if(!Anchor.IsNone())if(auto* Target=Locate(Anchor))G.Position=Target->GetActorLocation();return G;
}
FAetherInteractionTarget QueryTarget(AAetherFrontierCharacter* C,AActor* Actor)
{
 FAetherInteractionTarget R;
 if(!IsValid(C)||!IsValid(Actor)||!C->Alive()||C->bTravelPending||Actor==C||Actor->GetWorld()!=C->GetWorld())return R;
 const auto* Registry=C->GetWorld()->GetSubsystem<UAetherNearbyRegistry>();
 if(!Registry||!Registry->Contains(Actor))return R;
 auto* Target=Cast<AAetherFrontierProp>(Actor);auto* Downed=Cast<AAetherFrontierCharacter>(Actor);
 const bool Rescue=Downed&&Downed->Fighter==EAetherFighter::Player&&!Downed->Alive();
 if(!Target&&!Rescue)return R;
 if(Target&&(!Target->bEnabled||Target->Service.IsNone()||((IsPersonalFire(Target->Service)||Target->Reactive->bOwnerOnlyStimuli)&&Target->GetOwner()!=C)))return R;
 const double Radius=Rescue?200.:250.;
 if(FVector::DistSquared(C->GetActorLocation(),Actor->GetActorLocation())>FMath::Square(Radius))return R;
 FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherInteraction),false,C);Q.AddIgnoredActor(Actor);
 if(C->GetWorld()->LineTraceTestByChannel(C->GetActorLocation(),Actor->GetActorLocation(),ECC_Visibility,Q))return R;
 R.ProfileRevision=C->ProfileState()?C->ProfileState()->Profile.Revision:-1;
 const FString Key=TEXT("[")+C->BindingFor("Interact").GetDisplayName().ToString()+TEXT("] ");
 if(Rescue){R.Rescue=Downed;R.ActionId="Revive";R.bExecutable=true;R.Prompt=Key+TEXT("救援队友：保持靠近 3 秒");return R;}
 R.Prop=Target;R.StableId=Target->Spec.Id;R.ActionId=Target->Service;const FName S=Target->Service;
 static const TMap<FName,FString> Labels={{"SupplyA",TEXT("拾取补给")},{"SupplyB",TEXT("拾取补给")},{"Gate",TEXT("进入城镇")},{"Register",TEXT("与登记员交谈")},{"Inn",TEXT("绑定据点 / 休息")},{"Teacher",TEXT("学习能力 / 开始个人训练")},{"Shop",TEXT("购买生命药水")},{"Recruit",TEXT("招募同行者")},{"SealDelivered",TEXT("交付古印")},{"Daily",TEXT("结算补给委托")},{"DailyPatrol",TEXT("查看 / 结算巡逻委托")},{"DailyFire",TEXT("领取 / 结算灭火委托")},{"Well",TEXT("从水井补充有限储水")},{"Bucket",TEXT("倾倒水桶 / 检查已清理现场")},{"HingedGate",TEXT("切换门机关")},{"Source",TEXT("切换固定电源")},{"SupplyRestored",TEXT("操作机械水泵")},{"Receiver",TEXT("检查电动水泵")},{"Rescue",TEXT("救援工匠")},{"Abbey",TEXT("进入修道院挑战")},{"AbbeyValve",TEXT("引导阀门")},{"GuardianDefeated",TEXT("领取遭遇凭据")},{"Activity",TEXT("开始 / 引导中继防守")},{"Gather",TEXT("采集补给")},{"Loot",TEXT("拾取共享掉落")}};
 if(IsPersonalFire(S))R.Prompt=TEXT("瞄准个人火盆使用引泉，实际灭火才会完成目标");
 else if(Target->bInspectableFire){R.bExecutable=!Target->Reactive->State.bBurning;R.Prompt=R.bExecutable?Key+TEXT("检查清理后的火点"):TEXT("使用引泉或旁边水桶灭火");}
 else if(S=="Dummy")R.Prompt=TEXT("装备训练剑，轻击训练木桩");
 else if(Target->bCarryable)R.Prompt=TEXT("[")+C->BindingFor("Carry").GetDisplayName().ToString()+TEXT("] 瞄准物件搬运；可推移或投掷");
 else if(const auto* Label=Labels.Find(S)){R.Prompt=Key+*Label;R.bExecutable=true;}
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
