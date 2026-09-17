#include "AetherWorldAuthoring.h"
#include "AetherFrontier.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "GameFramework/WorldSettings.h"
#include "WorldPartition/WorldPartition.h"
#include "EngineUtils.h"
#if WITH_EDITOR
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Builders/CubeBuilder.h"
#include "ActorFactories/ActorFactory.h"
#endif
namespace
{
struct FShellPiece { FName Id;FVector Location;FVector Scale;bool Spatial=true; };
TArray<FShellPiece> Pieces()
{
 TArray<FShellPiece> P={
 {"Ground",{0,0,-400},{800,800,2},false},{"Town",{0,0,-60},{200,200,1.2},false},
 {"SouthRoad",{-6500,-19000,-50},{16,220,1},false},{"WestRoad",{-18000,0,-50},{180,16,1},false},
 {"EastRoad",{18000,0,-50},{180,16,1},false},{"NorthRoad",{0,18000,-50},{16,180,1},false},
 {"ForestFloor",{-27000,0,-60},{80,100,1.2},false},{"WorksWest",{26500,0,-60},{10,24,1.2},false},
 {"WorksEast",{28200,0,-60},{16,24,1.2},false},{"Maintenance",{27350,1000,-20},{9,3,.4},false},
 {"BridgeStopA",{26970,-600,-30},{.4,3,.6},false},{"BridgeStopB",{27730,-600,-30},{.4,3,.6},false},
 {"AbbeyFloor",{0,27000,-60},{70,80,1.2},false},{"ActivityFloor",{25000,22000,-60},{50,50,1.2},false},
 {"RingEast",{25000,12000,-50},{16,220,1},false},{"RingNorth",{12500,26000,-50},{250,16,1},false}};
 for(int I=0;I<6;++I)
 {const float X=(I%3-1)*1900.f,Y=I<3?-1800.f:1800.f;P.Add({*FString::Printf(TEXT("House%d"),I),{X,Y,220},{10,8,4.4},true});P.Add({*FString::Printf(TEXT("Roof%d"),I),{X,Y,480},{11,9,.8},true});}
 for(int I=0;I<8;++I)for(int Side:{-1,1})P.Add({*FString::Printf(TEXT("Pillar%d_%d"),I,Side),{Side*2200.f,24600.f+I*700,250},{1.2,1.2,5},true});
 return P;
}
}
bool UAetherWorldAuthoring::IsShellPiece(FName Id)
{static const auto Data=Pieces();return Data.ContainsByPredicate([Id](const auto& P){return P.Id==Id;});}
bool UAetherWorldAuthoring::BakeStaticShell(UWorld* World)
{
#if WITH_EDITOR
 if(!World||World->IsGameWorld()||!World->GetWorldPartition())return false;
 ANavMeshBoundsVolume* Bounds=nullptr;for(TActorIterator<ANavMeshBoundsVolume> It(World);It;++It){Bounds=*It;break;}
 if(!Bounds)Bounds=World->SpawnActor<ANavMeshBoundsVolume>(FVector(0,0,500),FRotator::ZeroRotator);
 if(Bounds->GetComponentsBoundingBox(true).GetVolume()<1.e6)
 {
  Bounds->SetActorLabel(TEXT("Aether_DynamicNavigationBounds"));Bounds->SetIsSpatiallyLoaded(false);
  auto* Builder=NewObject<UCubeBuilder>(Bounds);Builder->X=82000;Builder->Y=82000;Builder->Z=3000;
  // The factory creates the brush model and collision hull; calling Builder.Build alone leaves a zero-size volume.
  UActorFactory::CreateBrushForVolumeActor(Bounds,Builder);Bounds->MarkPackageDirty();
  if(Bounds->GetComponentsBoundingBox(true).GetVolume()<1.e6)return false;
  World->GetWorldSettings()->Tags.AddUnique("AetherNavBounds");World->GetWorldSettings()->MarkPackageDirty();
 }
 // Re-running preserves artist/editor changes rather than duplicating the authored shell.
 if(World->GetWorldSettings()->ActorHasTag("AetherPartitionShell"))return true;
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));if(!Cube)return false;
 World->GetWorldPartition()->SetEnableStreaming(true);
 World->GetWorldSettings()->DefaultGameMode=AAetherFrontierMode::StaticClass();
 for(const auto& Piece:Pieces())
 {
  auto* A=World->SpawnActor<AStaticMeshActor>(Piece.Location,FRotator::ZeroRotator);if(!A)return false;
  A->SetActorLabel(TEXT("Aether_")+Piece.Id.ToString());A->Tags.Add("AetherStaticShell");A->Tags.Add(Piece.Id);
  A->GetStaticMeshComponent()->SetStaticMesh(Cube);A->SetActorScale3D(Piece.Scale);
  A->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));A->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
  A->SetIsSpatiallyLoaded(Piece.Spatial);A->MarkPackageDirty();
 }
 auto* Sun=World->SpawnActor<ADirectionalLight>(FVector(0,0,1200),FRotator(-48,-30,0));
 Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);Sun->GetLightComponent()->SetIntensity(3);Sun->Tags.Add("AetherBakedLighting");Sun->SetIsSpatiallyLoaded(false);
 Cast<UDirectionalLightComponent>(Sun->GetLightComponent())->SetAtmosphereSunLight(true);
 auto* Sky=World->SpawnActor<ASkyLight>();Sky->SetIsSpatiallyLoaded(false);auto* Light=Sky->GetLightComponent();Light->SetMobility(EComponentMobility::Movable);Light->SourceType=SLS_SpecifiedCubemap;Light->bLowerHemisphereIsBlack=false;Light->SetIntensity(1.3f);
 Light->SetCubemap(LoadObject<UTextureCube>(nullptr,TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap")));
 auto* Air=World->SpawnActor<ASkyAtmosphere>();Air->SetIsSpatiallyLoaded(false);
 World->GetWorldSettings()->Tags.Add("AetherPartitionShell");World->GetWorldSettings()->MarkPackageDirty();
 return true;
#else
 return false;
#endif
}
