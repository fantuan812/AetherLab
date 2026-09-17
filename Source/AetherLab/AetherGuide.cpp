#include "AetherGuide.h"
#include "AetherFrontier.h"
#include "AetherRules.h"
#include "ReactiveWorldSubsystem.h"
#include "EngineUtils.h"
namespace AetherGuide
{
int32 SelectQuest(const FAetherProfile& P,int32 Preferred,bool Cycle)
{
 const int32 Count=FAetherRules::Get().Quests.Num();if(!Count)return INDEX_NONE;
 if(!Cycle&&P.Available(Preferred))return Preferred;
 const int32 Start=Cycle&&Preferred>=0&&Preferred<Count?(Preferred+1)%Count:0;
 for(int32 I=0;I<Count;++I){const int32 Q=(Start+I)%Count;if(P.Available(Q))return Q;}return INDEX_NONE;
}
FString ObjectiveLabel(FName Id){const auto* O=FAetherRules::Get().Objectives.Find(Id);return O?O->Label:Id.ToString();}
bool IsPersonalFire(FName Service){return Service=="TrainingExtinguished"||Service=="DailyFire0"||Service=="DailyFire1"||Service=="DailyFire2";}
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
 if(G.Quest==INDEX_NONE){G.Title=TEXT("主线已完成");G.Label=TEXT("镇上委托与中继防守已开放");G.Hint=TEXT("到公告板领取每日委托。");G.Position=FVector(-900,300,100);G.bHasTarget=P.Claims.Contains(FAetherProfile::QuestId(7));return G;}
 G.Title=FAetherProfile::QuestTitle(G.Quest);G.bRewardReady=P.Complete(G.Quest);
 if(G.bRewardReady){G.Label=TEXT("目标完成，奖励待领");G.Hint=TEXT("整理背包后，在任务面板领取奖励。");return G;}
 for(auto Id:FAetherProfile::Objectives(G.Quest))if(!P.Evidence.Contains(Id)){G.Objective=Id;break;}
 const auto* O=FAetherRules::Get().Objectives.Find(G.Objective);if(!O)return G;
 G.Label=O->Label;G.Hint=O->Hint;G.Position=O->Position;G.bHasTarget=true;FName Anchor=O->Anchor;
 auto Locate=[&](FName Id)->AAetherFrontierProp*{for(TActorIterator<AAetherFrontierProp> It(C->GetWorld());It;++It)if(It->Spec.Id==Id)return *It;return nullptr;};
 if(G.Quest==2)
 {
  const FName PersonalId=*FString(TEXT("Training_")+P.CharacterId);auto* Fire=Locate(PersonalId);AAetherFrontierCharacter* Trainer=nullptr;
  for(TActorIterator<AAetherFrontierCharacter> It(C->GetWorld());It;++It)if(It->Fighter==EAetherFighter::ShieldGuard&&It->Alive()&&It->GetOwner()==C){Trainer=*It;break;}
  const bool NeedTeacher=(P.LearnedSpells&3)!=3||(!P.Evidence.Contains("TrainingExtinguished")&&!Fire)||(!P.Evidence.Contains("Block")&&!Trainer);
  if(NeedTeacher){Anchor="Teacher";G.Position=FVector(400,-300,90);G.Label=TEXT("拜访导师，准备个人训练");G.Hint=TEXT("与导师交谈，学习引焰 / 引泉并生成训练目标。");}
  else if(G.Objective=="TrainingExtinguished"&&Fire){Anchor=PersonalId;G.Position=Fire->GetActorLocation();}
  else if(G.Objective=="Block"&&Trainer){Anchor=NAME_None;G.Position=Trainer->GetActorLocation();}
 }
 if(G.Quest==6)for(TActorIterator<AAetherEncounterDirector> It(C->GetWorld());It;++It)if(It->Abbey.Participants.Contains(P.CharacterId))
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
FAetherInteractionTarget SelectInteraction(AAetherFrontierCharacter* C)
{
 FAetherInteractionTarget R;if(!C||!C->Alive())return R;
 auto Visible=[&](AActor* Target){FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherInteraction),false,C);Q.AddIgnoredActor(Target);return !C->GetWorld()->LineTraceTestByChannel(C->GetActorLocation(),Target->GetActorLocation(),ECC_Visibility,Q);};
 double Best=FMath::Square(200.);
 for(TActorIterator<AAetherFrontierCharacter> It(C->GetWorld());It;++It)if(*It!=C&&It->Fighter==EAetherFighter::Player&&!It->Alive())
 {const double D=FVector::DistSquared(C->GetActorLocation(),It->GetActorLocation());if(D<Best&&Visible(*It)){Best=D;R.Rescue=*It;}}
 const FString Key=TEXT("[")+C->BindingFor("Interact").GetDisplayName().ToString()+TEXT("] ");
 if(R.Rescue.IsValid()){R.Prompt=Key+TEXT("救援队友：保持靠近 3 秒");return R;}
 Best=FMath::Square(250.);
 for(TActorIterator<AAetherFrontierProp> It(C->GetWorld());It;++It)if(!It->Service.IsNone())
 {
  if((IsPersonalFire(It->Service)||It->Reactive->bOwnerOnlyStimuli)&&It->GetOwner()!=C)continue;
  const double D=FVector::DistSquared(C->GetActorLocation(),It->GetActorLocation());if(D>=Best||!Visible(*It))continue;Best=D;R.Prop=*It;
 }
 auto* Target=R.Prop.Get();if(!Target)return R;const FName S=Target->Service;
 static const TMap<FName,FString> Labels={{"SupplyA",TEXT("拾取补给")},{"SupplyB",TEXT("拾取补给")},{"Gate",TEXT("进入城镇")},{"Register",TEXT("与登记员交谈")},{"Inn",TEXT("绑定据点 / 休息")},{"Teacher",TEXT("学习能力 / 开始个人训练")},{"Shop",TEXT("购买生命药水")},{"Recruit",TEXT("招募同行者")},{"SealDelivered",TEXT("交付古印")},{"Daily",TEXT("结算补给委托")},{"DailyPatrol",TEXT("查看 / 结算巡逻委托")},{"DailyFire",TEXT("领取 / 结算灭火委托")},{"Well",TEXT("从水井补充有限储水")},{"Bucket",TEXT("倾倒水桶 / 检查已清理现场")},{"HingedGate",TEXT("切换门机关")},{"Source",TEXT("切换固定电源")},{"SupplyRestored",TEXT("操作机械水泵")},{"Receiver",TEXT("检查电动水泵")},{"Rescue",TEXT("救援工匠")},{"Abbey",TEXT("进入修道院挑战")},{"AbbeyValve",TEXT("引导阀门")},{"GuardianDefeated",TEXT("领取遭遇凭据")},{"Activity",TEXT("开始 / 引导中继防守")},{"Gather",TEXT("采集补给")},{"Loot",TEXT("拾取共享掉落")}};
 if(IsPersonalFire(S))R.Prompt=TEXT("瞄准个人火盆使用引泉，实际灭火才会完成目标");
 else if(S.ToString().StartsWith("ForestFire"))R.Prompt=Target->Reactive->State.bBurning?TEXT("使用引泉或旁边水桶灭火"):Key+TEXT("检查清理后的火点");
 else if(S=="Dummy")R.Prompt=TEXT("装备训练剑，轻击训练木桩");
 else if(Target->bCarryable)R.Prompt=TEXT("[")+C->BindingFor("Carry").GetDisplayName().ToString()+TEXT("] 瞄准物件搬运；可推移或投掷");
 else if(const auto* Label=Labels.Find(S))R.Prompt=Key+*Label;
 else if(S.ToString().StartsWith("Patrol"))R.Prompt=Key+TEXT("记录巡逻位置");
 else if(S=="Support")R.Prompt=TEXT("用训练剑切断，或用引焰烧毁支撑绳索");
 else if(S=="Water")R.Prompt=TEXT("对浅水释放霜凝，可形成承重冰面");
 return R;
}
}
