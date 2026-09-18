#include "../AetherFrontier.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
void AAetherFrontierMode::CheckReactions()
{
 auto* C=Cast<AAetherFrontierCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
 if(!C||!C->ProfileState()){if(Elapsed>8)FPlatformMisc::RequestExitWithStatus(false,1);return;}
 auto Check=[&](bool Pass,const TCHAR* Name){UE_LOG(LogTemp,Display,TEXT("REACTION_CHECK %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),Name);if(!Pass)++Failures;};
 auto* W=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();
 if(SmokeStage==0)
 {
  GetGameState<AAetherFrontierState>()->bPowerOn=false;Prop("PowerSource")->Mechanism->bPowerEnabled=false;
  C->ProfileState()->Profile.Claims={FAetherProfile::QuestId(0),FAetherProfile::QuestId(1),FAetherProfile::QuestId(2)};
  auto* Bucket=Prop("Bucket0");const double Before=W->GetSimulation()->Find(Bucket->Reactive->GetBodyId())->WaterKg;
  C->SetActorLocation(Bucket->GetActorLocation()+FVector(0,150,50));Interact(C);
  const double After=W->GetSimulation()->Find(Bucket->Reactive->GetBodyId())->WaterKg;
  Check(After<Before&&After>.4,TEXT("Pour only debits receivable water and keeps excess"));
  Check(W->GetSimulation()->GetStats().TransferredWaterKg>0,TEXT("Bucket uses authoritative transfer ledger"));
  auto* Crate=Prop("Crate");Crate->Mechanism->ImpactCreditSeconds=.15;Crate->Mechanism->RecordImpactSource(C);
  Check(Crate->Mechanism->GetImpactSource()==C,TEXT("Fresh physical manipulation credits operator"));
  auto* Sword=C->Equipment->Catalog?C->Equipment->Catalog->Find("TrainingSword"):nullptr;
  const auto* Attack=Sword?Sword->FindAttack("Light"):nullptr;
  Check(Attack&&Attack->CuttingWorkJ>=20,TEXT("Saved basic sword data includes blade work"));
  if(Attack){FAetherEquipmentHit Hit;Hit.Source=C;Hit.ItemId="TrainingSword";Hit.AttackId="Light";Hit.CuttingWorkJ=Attack->CuttingWorkJ;Hit.ImpulseNs=FVector::ZeroVector;Prop("WorksRope")->ReceiveEquipmentHit_Implementation(Hit);}
  SmokeStage=1;return;
 }
 if(SmokeStage==1&&Elapsed>3)
 {
  Check(Prop("WorksRope")->Reactive->State.bBroken,TEXT("Blade work alone severs authored rope"));
  Check(Prop("WorksBridge")->Mechanism->bReleased&&Prop("WorksBridge")->Mesh->IsSimulatingPhysics(),TEXT("Broken support releases real rigid body"));
  Check(Prop("WorksBridge")->Mechanism->GetImpactSource()==C,TEXT("Released bridge inherits finite break attribution"));
  Check(Prop("Crate")->Mechanism->GetImpactSource()==nullptr,TEXT("Old manipulation credit expires"));
  Check(C->ProfileState()->Profile.Evidence.Contains("ForestFire0"),TEXT("Real finite bucket water credits extinguish"));
  const double Before=W->GetSimulation()->Find(Prop("Bucket0")->Reactive->GetBodyId())->WaterKg;
  Interact(C);Check(FMath::IsNearlyEqual(Before,W->GetSimulation()->Find(Prop("Bucket0")->Reactive->GetBodyId())->WaterKg,1.e-8),TEXT("Cleared site inspection consumes no further water"));
  TArray<FReactiveSaveRecord> Records;
  if(!W->Capture(Records)&&Elapsed<5)return;
  Check(!Records.IsEmpty()&&Records[0].MaterialSchema==1,TEXT("New material signature schema captured"));
  if(!Records.IsEmpty())
  {
   auto Invalid=Records;Invalid[0].MaterialSchema=99;Check(!W->Restore(Invalid),TEXT("Unknown material schema rejected atomically"));
   Check(W->Restore(Records)&&Prop("WorksRope")->Reactive->State.bBroken&&Prop("WorksBridge")->Mechanism->bReleased,TEXT("Cut structure survives material snapshot restore"));
   Check(Prop("WorksBridge")->Mechanism->GetImpactSource()==nullptr,TEXT("Restored structure does not resurrect old player credit"));
  }
  FString LegacySlot;
  if(FParse::Value(FCommandLine::Get(),TEXT("AetherLegacyFixture="),LegacySlot)&&!LegacySlot.IsEmpty())
  {
   auto* Legacy=Cast<UAetherFrontierSave>(UGameplayStatics::LoadGameFromSlot(LegacySlot,0));
   Check(Legacy&&!Legacy->World.IsEmpty()&&Legacy->World[0].MaterialSchema==0,TEXT("Original pre-change save loads with legacy signature schema"));
   Check(Legacy&&W->Restore(Legacy->World),TEXT("Original legacy material snapshot restores under new cutting rules"));
  }
  UE_LOG(LogTemp,Display,TEXT("AETHER_REACTION_%s failures=%d"),Failures?TEXT("FAIL"):TEXT("PASS"),Failures);SmokeStage=2;
  FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
 }
}
