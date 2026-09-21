#include "../AetherFrontier.h"
#include "Quests/AetherGuide.h"
#include "../AetherInventoryRules.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
void AAetherFrontierMode::CheckDataContracts()
{
 auto* C=Cast<AAetherFrontierCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
 if(!C||!C->ProfileState()){if(Elapsed>8)FPlatformMisc::RequestExitWithStatus(false,1);return;}
 auto* W=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();
 auto Check=[&](bool Pass,const TCHAR* Id){UE_LOG(LogTemp,Display,TEXT("DATA_CHECK %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),Id);if(!Pass)++Failures;};
 auto MoveTo=[&](AAetherFrontierProp* P){C->SetActorLocation(P->GetActorLocation()+FVector(0,-150,70));};
 if(SmokeStage==0)
 {
  FString Text;FFileHelper::LoadFileToString(Text,*(FPaths::ProjectDir()/TEXT("Docs/Fixtures/V802.rules.json")));const auto R=FAetherRules::Parse(Text);
  FAetherProfile Fixture;Fixture.Add("SurveySword",1,R);Fixture.Equip(Fixture.Inventory.Last().InstanceId,R);TArray<FAetherEquippedSlot> Slots;
  Check(R.bValid&&AetherInventory::ValidateCatalog(R,C->Equipment->Catalog)&&AetherInventory::BuildLoadout(Fixture,R,Slots)&&C->Equipment->RestoreLoadout(Slots)&&C->Equipment->VisualForSlot("MainHand"),TEXT("AUD8-04 new item definition restores runtime basic sword"));
  const int32 Revision=C->Equipment->LoadoutRevision;auto Bad=Slots;Bad.Append(Slots);
  Check(!C->Equipment->RestoreLoadout(Bad)&&C->Equipment->LoadoutRevision==Revision&&C->Equipment->Slots.Num()==1,TEXT("AUD8-05 invalid whole runtime loadout leaves equipment unchanged"));
  auto* Cold=Make("ColdReceiver",NAME_None,{4000,-4000,50},FVector(.6),EAetherObjectKind::Timber,TEXT(""));
  auto* Bucket=Make("PortableReservoir","Bucket",{4000,-4300,50},FVector(.6),EAetherObjectKind::Water,TEXT(""));
  MoveTo(Bucket);const double Before=Bucket->Reactive->State.WaterKg;
  Check(AetherGuide::SelectWaterReceiver(C,Bucket)==Cold,TEXT("AUD8-08 cold unnamed material selected by receive capability"));
  Interact(C);Check(Bucket->Reactive->State.WaterKg<Before&&Cold->Reactive->State.WaterKg>0&&!Cold->Reactive->State.bBurning,TEXT("AUD8-08 finite water transfers to non-burning receiver"));
  const double FullBefore=Bucket->Reactive->State.WaterKg;Interact(C);
  Check(FMath::IsNearlyEqual(FullBefore,Bucket->Reactive->State.WaterKg,1.e-8),TEXT("AUD8-09 full receiver rejects without debit"));
  auto* Private=Make("PrivateReceiver",NAME_None,{4000,-4200,50},FVector(.2),EAetherObjectKind::Timber,TEXT(""));Private->Reactive->bOwnerOnlyStimuli=true;Private->SetOwner(Prop("Teacher"));
  Check(AetherGuide::SelectWaterReceiver(C,Bucket)!=Private,TEXT("AUD8-09 foreign private receiver not selected"));
  Private->Destroy();Props.Remove(Private);
  auto* Wall=Make("PourWall",NAME_None,{4000,-4150,100},{2,.2,3},EAetherObjectKind::Stone,TEXT(""));
  Interact(C);Check(!AetherGuide::SelectWaterReceiver(C,Bucket)&&FMath::IsNearlyEqual(FullBefore,Bucket->Reactive->State.WaterKg,1.e-8),TEXT("AUD8-09 obstruction rejects without debit"));
  Wall->Destroy();Props.Remove(Wall);
  auto* Fire=Make("RenamedBrazier",NAME_None,{7000,-4000,50},FVector(.6),EAetherObjectKind::Timber,TEXT(""));
  Make("OtherReservoir","Bucket",{7000,-4300,50},FVector(.6),EAetherObjectKind::Water,TEXT(""));
  FReactiveStimulus Heat;Heat.HeatJ=60000;Fire->Reactive->Inject(Heat);SmokeStage=1;return;
 }
 if(SmokeStage==1&&Elapsed>3)
 {
  auto* Fire=Prop("RenamedBrazier");auto* Bucket=Prop("OtherReservoir");MoveTo(Bucket);
  Check(Fire->Reactive->State.bBurning&&AetherGuide::SelectWaterReceiver(C,Bucket)==Fire,TEXT("AUD8-08 same container rule selects renamed fire in second area"));
  Interact(C);SmokeStage=2;return;
 }
 if(SmokeStage==2&&Elapsed>4)
 {
  Check(!Prop("RenamedBrazier")->Reactive->State.bBurning,TEXT("AUD8-08 renamed fire extinguished by actual transferred water"));
  Check(C->ProfileState()->Profile.Evidence.IsEmpty()&&C->ProfileState()->Profile.Claims.IsEmpty(),TEXT("AUD8-08 watering unbound materials does not fabricate quest progress"));
  UE_LOG(LogTemp,Display,TEXT("AETHER_DATA_%s failures=%d"),Failures?TEXT("FAIL"):TEXT("PASS"),Failures);SmokeStage=3;FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
 }
}
