#include "../AetherFrontier.h"
#include "../AetherRules.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"
void AAetherFrontierMode::CheckGuidance()
{
 auto* C=Cast<AAetherFrontierCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
 if(!C||!C->ProfileState()){if(Elapsed>8)FPlatformMisc::RequestExitWithStatus(false,1);return;}
 auto Check=[&](bool Pass,const TCHAR* Name){UE_LOG(LogTemp,Display,TEXT("GUIDANCE_CHECK %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),Name);if(!Pass)++Failures;};
 auto* PS=C->ProfileState();
 if(SmokeStage==0)
 {
  Check(FAetherRules::Get().bValid,TEXT("All quest labels and anchors validate"));
  FAetherProfile P;Check(AetherGuide::SelectQuest(P,7)==0,TEXT("Locked preference falls back to first available"));
  P.Claims={FAetherProfile::QuestId(0),FAetherProfile::QuestId(1),FAetherProfile::QuestId(2)};
  Check(AetherGuide::SelectQuest(P,3,true)==4&&AetherGuide::SelectQuest(P,4,true)==3,TEXT("Independent field quests cycle"));
  P.Claims.Add(FAetherProfile::QuestId(3));Check(AetherGuide::SelectQuest(P,3)==4,TEXT("Claimed quest automatically advances"));
  P.Claims.Add(FAetherProfile::QuestId(4));Check(AetherGuide::SelectQuest(P,3)==5,TEXT("Recruit unlock requires both branches"));
  PS->Profile.Claims={FAetherProfile::QuestId(0),FAetherProfile::QuestId(1),FAetherProfile::QuestId(2)};
  C->TrackedQuest=3;auto G=AetherGuide::Resolve(C);Check(G.Objective=="ForestFire0"&&G.bHasTarget&&G.Position.Equals(Prop("ForestFire0")->GetActorLocation()),TEXT("World marker targets actual objective"));
  C->SetActorLocation(Prop("ForestFire0")->GetActorLocation()+FVector(0,-160,40));
  Check(AetherGuide::SelectInteraction(C).Prop==Prop("ForestFire0"),TEXT("Prompt and server select the same nearby target"));
  Check(Prop("ForestFire0")->Reactive->State.bBurning,TEXT("Bypass check starts with an actual burning fire"));
  Interact(C);Check(!PS->Profile.Evidence.Contains("ForestFire0"),TEXT("Interact cannot bypass burning fire"));
  Check(!Prop("FireBoard")->Reactive->bOwnerOnlyStimuli&&Prop("FireBoard")->Reactive->StableId=="FireBoard",TEXT("Commission board is public and persistent"));
  const FVector TestLocation(-2000,-2000,50);C->SetActorLocation(TestLocation+FVector(0,-150,50));
  auto* Own=Make("GuideTestFire","TrainingExtinguished",TestLocation,FVector(.6),EAetherObjectKind::Timber,TEXT(""));Own->SetOwner(C);
  Check(AetherGuide::SelectInteraction(C).Prop==Own,TEXT("Personal prop visible to its owner"));
  Own->SetOwner(Prop("Teacher"));Check(AetherGuide::SelectInteraction(C).Prop!=Own,TEXT("Another owner personal prop is not selectable"));
  Own->Destroy();Props.Remove(Own);
  C->SetActorLocation(Prop("ForestFire0")->GetActorLocation()+FVector(0,-160,40));
  for(int I=0;I<3;++I){FReactiveStimulus Water;Water.SourceActor=C;Water.WaterKg=1;Prop(*FString::Printf(TEXT("ForestFire%d"),I))->Reactive->Inject(Water);}
  SmokeStage=1;return;
 }
 if(SmokeStage==1&&Elapsed>3)
 {
  bool Cleared=true;for(int I=0;I<3;++I)Cleared&=AetherGuide::CanInspectFire(Prop(*FString::Printf(TEXT("ForestFire%d"),I)));
  if(!Cleared&&Elapsed<5)return;
  for(int I=0;I<3;++I){const auto* Fire=Prop(*FString::Printf(TEXT("ForestFire%d"),I));const auto& S=Fire->Reactive->State;UE_LOG(LogTemp,Display,TEXT("GUIDANCE_FIRE %d temperature=%.2f fuel=%.6f burning=%d"),I,S.TemperatureC,S.FuelKg,S.bBurning);}
  Check(Cleared,TEXT("Actual water reaction produces inspectable sites"));
  Check(PS->Profile.Evidence.Contains("ForestFire0"),TEXT("Real extinguish event credits participating character"));
  C->SetActorLocation(Prop("Rescue")->GetActorLocation()+FVector(0,-160,40));Interact(C);
  Check(PS->Profile.Claims.Contains(FAetherProfile::QuestId(3)),TEXT("Safe rescue completes forest quest"));
  const auto G=AetherGuide::Resolve(C);Check(G.Quest==4&&G.Objective=="SupplyRestored",TEXT("Guide advances after world completion"));
  UE_LOG(LogTemp,Display,TEXT("AETHER_GUIDANCE_%s failures=%d"),Failures?TEXT("FAIL"):TEXT("PASS"),Failures);SmokeStage=2;
  FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
 }
}
