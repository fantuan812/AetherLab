#include "Framework/AetherFrontier.h"
#include "Presentation/AetherPresentation.h"
#include "GameFramework/HUD.h"
#include "Assets/AetherAssetPreload.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
void AAetherFrontierMode::CheckV9()
{
#if !UE_BUILD_SHIPPING
 auto* C=Cast<AAetherFrontierCharacter>(UGameplayStatics::GetPlayerPawn(this,0));if(!C||!C->ProfileState()||C->bTravelPending)return;
 auto* PS=C->ProfileState();auto* W=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();
 if(Elapsed<3||W->GetSimulation()->HasPendingInputs())return;
 auto Check=[&](bool OK,const TCHAR* Name){if(!OK){++Failures;UE_LOG(LogTemp,Error,TEXT("AETHER_V9_FAIL %s"),Name);}else UE_LOG(LogTemp,Display,TEXT("AETHER_V9_CHECK %s"),Name);};
 FString Phase;FParse::Value(FCommandLine::Get(),TEXT("AetherV9Phase="),Phase);
 if(Phase=="Reload")
 {
  Check(PS->Profile.InventoryReceipts.Num()==2,TEXT("durable_inventory_receipts"));
  Check(PS->Profile.Count("Potion")==34,TEXT("restart_quantity"));
  const auto* Offline=Database->World.FindByPredicate([](const auto& R){return R.StableId=="FieldBucket";});
  Check(Offline&&Offline->WaterKg<.5,TEXT("restart_unloaded_water"));
  if(PS->Profile.InventoryReceipts.Num()){auto R=PS->Profile.InventoryReceipts.Last();int32 Rev=0,Moved=0;Check(ExecuteInventory(C,R.Command,Rev,Moved)==EAetherInventoryResult::Applied&&PS->Profile.Count("Potion")==34,TEXT("restart_retry_no_duplicate"));}
  UE_LOG(LogTemp,Display,TEXT("AETHER_V9_%s phase=Reload failures=%d"),Failures?TEXT("FAIL"):TEXT("PASS"),Failures);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);return;
 }
 if(Elapsed<ClosureAt)return;ClosureAt=Elapsed+.5f;
 if(SmokeStage==0)
 {
  // 在真实地图/连接中验证工厂装配，而非只检查静态类注册。
  auto* PC=Cast<APlayerController>(C->Controller);
  Check(PC&&PC->GetHUD()&&PC->GetHUD()->GetClass()==AetherPresentation::ResolveHUD(),TEXT("local_hud_factory"));
  auto P=PS->Profile;P.Inventory.Reset();P.Equipped.Reset();P.InventoryReceipts.Reset();P.Gold=100;P.Add("Potion",35);
  Check(Commit(PS,P),TEXT("fixture_commit"));C->SetActorLocation(FVector(-14000,10000,130));UpdateRegions({C->GetActorLocation()});SmokeStage=1;return;
 }
 if(SmokeStage==1)
 {
  auto Cmd=FAetherInventoryCommand();Cmd.CommandId=FGuid(0xA379,0,0,1);Cmd.ExpectedInventoryRevision=PS->Profile.Revision;Cmd.Action="Split";Cmd.ItemInstanceId=PS->Profile.Inventory[1].InstanceId;Cmd.Quantity=3;int32 Rev=0,N=0;C->NextServerAction=0;
  Check(ExecuteInventory(C,Cmd,Rev,N)==EAetherInventoryResult::Applied&&N==3,TEXT("selected_split"));
  const int Count=PS->Profile.Inventory.Num();Check(ExecuteInventory(C,Cmd,Rev,N)==EAetherInventoryResult::Applied&&PS->Profile.Inventory.Num()==Count,TEXT("retry_idempotent"));
  Cmd.Quantity=2;Check(ExecuteInventory(C,Cmd,Rev,N)==EAetherInventoryResult::CommandConflict,TEXT("same_id_changed_payload"));
  Cmd.CommandId=FGuid::NewGuid();C->NextServerAction=0;Check(ExecuteInventory(C,Cmd,Rev,N)==EAetherInventoryResult::StaleRevision,TEXT("stale_version_rejected"));
  C->PendingInventory=FAetherInventoryCommand();C->PendingInventory.CommandId=FGuid(0xA379,0,0,2);C->PendingInventory.ExpectedInventoryRevision=PS->Profile.Revision;C->PendingInventory.Action="Use";C->PendingInventory.ItemInstanceId=PS->Profile.Inventory.Last().InstanceId;
  C->SetVitals(40,100,100);C->NextServerAction=0;bFailAfterDataWrite=true;
  Check(ExecuteInventory(C,C->PendingInventory,Rev,N)==EAetherInventoryResult::StorageUnavailable&&PS->Profile.Count("Potion")==35&&C->Health()==40,TEXT("post_data_failure_atomic"));bFailAfterDataWrite=false;SmokeStage=2;return;
 }
 if(SmokeStage==2)
 {
  int32 Rev=0,N=0;C->NextServerAction=0;Check(ExecuteInventory(C,C->PendingInventory,Rev,N)==EAetherInventoryResult::Applied&&PS->Profile.Count("Potion")==34,TEXT("retry_after_failed_storage"));
  auto* Bucket=Prop("FieldBucket");Check(Bucket!=nullptr,TEXT("third_scene_loaded"));if(Bucket)W->WithdrawWater(Bucket->Reactive,.2);
  if(auto* Crate=Prop("FieldCrate")){Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);Crate->Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);Crate->SetActorLocation(FVector(500,500,100),false,nullptr,ETeleportType::TeleportPhysics);Crate->Mesh->PutAllRigidBodiesToSleep();}
  bFailWrites=true;UpdateRegions({FVector(0,0,140)});Check(Prop("FieldBucket")!=nullptr,TEXT("failed_save_keeps_region_alive"));bFailWrites=false;
  SmokeStage=3;return;
 }
 if(SmokeStage==3)
 {
  C->SetActorLocation(FVector(0,0,140));UpdateRegions({C->GetActorLocation()});
  Check(!Prop("FieldBucket")&&!Prop("FieldSource"),TEXT("far_entities_destroyed"));
  Check(Prop("FieldCrate")!=nullptr,TEXT("migrated_crate_survives_origin_unload"));
  Check(Database->World.ContainsByPredicate([](const auto& R){return R.StableId=="FieldBucket"&&R.WaterKg<.5;}),TEXT("offline_water_retained"));
  Check(SaveWorld(),TEXT("save_while_region_absent"));SmokeStage=4;return;
 }
 if(SmokeStage==4)
 {
  UpdateRegions({FVector(-14000,10000,130),FVector(0,0,140)});
  auto* Bucket=Prop("FieldBucket");Check(Bucket&&Bucket->Reactive->State.WaterKg<.5,TEXT("loaded_state_restored"));
  Check(Prop("FieldCrate")!=nullptr,TEXT("two_player_union"));
  const auto* R=Database->World.FindByPredicate([](const auto& V){return V.StableId=="FieldCrate";});
  Check(R&&R->RegionId==AetherWorldState::RegionFor(FVector(500,500,100)),TEXT("cross_region_identity_migrates"));
  SmokeStage=5;return;
 }
 if(SmokeStage==5)
 {
  UpdateRegions({FVector(0,0,140)});Check(SaveWorld(),TEXT("final_durable_snapshot"));
  UE_LOG(LogTemp,Display,TEXT("AETHER_V9_%s phase=Run failures=%d"),Failures?TEXT("FAIL"):TEXT("PASS"),Failures);
  FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
 }
#endif
}
