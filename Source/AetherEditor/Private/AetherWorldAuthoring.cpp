#include "AetherWorldAuthoring.h"
#include "Framework/AetherFrontierMode.h"
#include "World/AetherShellDefinition.h"
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
  // 工厂同时创建刷体模型和碰撞壳；单独 Build 会留下零体积导航边界。
  UActorFactory::CreateBrushForVolumeActor(Bounds,Builder);Bounds->MarkPackageDirty();
  if(Bounds->GetComponentsBoundingBox(true).GetVolume()<1.e6)return false;
  World->GetWorldSettings()->Tags.AddUnique("AetherNavBounds");World->GetWorldSettings()->MarkPackageDirty();
 }
 // 已烘焙地图保持作者后续编辑；重复运行不能再次生成整套外壳。
 if(World->GetWorldSettings()->ActorHasTag("AetherPartitionShell"))return true;
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));if(!Cube)return false;
 World->GetWorldPartition()->SetEnableStreaming(true);
 World->GetWorldSettings()->DefaultGameMode=AAetherFrontierMode::StaticClass();
 for(const auto& Piece:AetherShell::Pieces())
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
