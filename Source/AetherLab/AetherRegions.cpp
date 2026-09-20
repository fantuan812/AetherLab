#include "AetherFrontier.h"
#include "AetherWorldDefinition.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/PlatformMemory.h"
#include "Engine/LevelStreaming.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

void AAetherFrontierMode::UpdateRegions(const TArray<FVector>& Players)
{
 if(Players.IsEmpty()&&!bNativeRegionBarrier)return;
 auto* W=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();if(W->GetSimulation()->HasPendingInputs())return;
 const auto& Definitions=FAetherWorldDefinitions::Get();
 TSet<FName> Wanted;
 for(const auto& E:Definitions.Objects)
 {
  auto* A=Prop(E.Id);const auto* Saved=Database->World.FindByPredicate([&](const auto& R){return R.StableId==E.Id;});
  const FVector Position=A?A->GetActorLocation():Saved?Saved->Transform.GetLocation():E.Location;
  bool Near=!E.bStream||(A&&A->Carrier);
  for(auto P:Players)Near|=FVector::DistSquared(P,Position)<FMath::Square(A?9500.:8000.);
  // A moving or recently disturbed object must settle before its simulation can freeze.
  if(A&&A->Mesh->IsSimulatingPhysics())Near|=A->Mesh->GetPhysicsLinearVelocity().SizeSquared()>100||A->Mesh->GetPhysicsAngularVelocityInDegrees().SizeSquared()>100;
  if(Near)Wanted.Add(E.Id);
 }
 // Support dependencies load/unload as a group; liquid/electrical boundaries disconnect.
 for(int Pass=0;Pass<Definitions.Objects.Num();++Pass){int Before=Wanted.Num();for(const auto& E:Definitions.Objects)for(auto Id:E.Supports)if(Wanted.Contains(E.Id)||Wanted.Contains(Id)){Wanted.Add(E.Id);Wanted.Add(Id);}if(Before==Wanted.Num())break;}
 TArray<FName> Unload;TArray<const FAetherWorldPlacement*> Load;
 for(const auto& E:Definitions.Objects)if(E.bStream)
 {if(Prop(E.Id)&&!Wanted.Contains(E.Id))Unload.Add(E.Id);else if(!Prop(E.Id)&&Wanted.Contains(E.Id))Load.Add(&E);}
 if(bNativeMode){AdvanceNativeRegions(Unload,Load);return;}
 if(Unload.IsEmpty()&&Load.IsEmpty())return;
 // Freeze commits all dirty loaded state first. Failed writes keep every actor alive.
 if(!SaveWorld())return;
 for(auto Id:Unload)if(auto* A=Prop(Id)){A->ResetFragments();Registry.Remove(Id);Props.Remove(A);A->Destroy();++RegionUnloads;}
 TArray<FReactiveSaveRecord> Restore;TArray<AAetherFrontierProp*> Spawned;
 for(const auto* E:Load)
 {
  if(auto* A=SpawnPlacement(*E)){Spawned.Add(A);if(const auto* R=Database->World.FindByPredicate([&](const auto& S){return S.StableId==E->Id;}))Restore.Add(*R);}
 }
 // Partial restore changes only known loaded identities and never discards offline records.
 if(!Restore.IsEmpty()&&!W->Restore(Restore,true))
 {
  for(auto* A:Spawned){Registry.Remove(A->Spec.Id);Props.Remove(A);A->Destroy();}
  UE_LOG(LogTemp,Error,TEXT("AETHER_REGION_RESTORE_REJECTED"));RebuildWorldLinks();return;
 }
 for(auto* A:Spawned){if(A->bCarryable)A->Mesh->SetSimulatePhysics(true);A->bWasBurning=A->Reactive->State.bBurning;A->bExtinguished=A->bInspectableFire&&!A->bWasBurning&&A->Reactive->State.FuelKg<A->Reactive->GetMaterial().InitialFuelKg-1.e-8;++RegionLoads;}
 RebuildWorldLinks();
 int32 CellsLoaded=0,CellsVisible=0;for(auto* Level:GetWorld()->GetStreamingLevels())if(Level){CellsLoaded+=Level->IsLevelLoaded()?1:0;CellsVisible+=Level->IsLevelVisible()?1:0;}
 UE_LOG(LogTemp,Display,TEXT("AETHER_STREAM cells_loaded=%d cells_visible=%d contacts_ms=%.3f"),CellsLoaded,CellsVisible,W->LastContactMilliseconds);
 UE_LOG(LogTemp,Display,TEXT("AETHER_REGION loaded=%d unloaded=%d registry=%d saved=%d reactive=%u solve_ms=%.3f memory_mb=%llu"),RegionLoads,RegionUnloads,Registry.Loaded.Num(),Database->World.Num(),W->GetSimulation()->GetStats().Registered,W->LastStepMilliseconds,FPlatformMemory::GetStats().UsedPhysical/(1024*1024));
}
