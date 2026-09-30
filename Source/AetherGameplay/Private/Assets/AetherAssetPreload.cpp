#include "Assets/AetherAssetPreload.h"
#include "Assets/AetherContent.h"
#include "Engine/AssetManager.h"
#include "AetherMotionBinding.h"
void UAetherAssetPreload::Initialize(FSubsystemCollectionBase& C)
{
 Super::Initialize(C);
 RootHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(FSoftObjectPath(TEXT("/Game/AetherCore/Data/DA_GameContent.DA_GameContent")));
}
void UAetherAssetPreload::Tick(float)
{
 if(bRequested||!RootHandle||!RootHandle->HasLoadCompleted())return;
 Content=Cast<UAetherGameContent>(RootHandle->GetLoadedAsset());bRequested=true;
 if(!Content||!Content->EquipmentCatalog){bFailed=true;UE_LOG(LogTemp,Error,TEXT("AETHER_ASSET_ROOT_FAILED"));return;}
 TArray<FSoftObjectPath> Paths;
 for(auto* D:{Content->Player.Get(),Content->Guard.Get(),Content->Caster.Get(),Content->Boss.Get()})if(D)
 {Paths.AddUnique(D->BodyMesh.ToSoftObjectPath());Paths.AddUnique(D->AnimationClass.ToSoftObjectPath());Paths.AddUnique(D->WalkAnimation.ToSoftObjectPath());Paths.AddUnique(D->AttackAnimation.ToSoftObjectPath());}
 for(const auto& D:Content->EquipmentCatalog->Items)if(D){Paths.AddUnique(D->Mesh.ToSoftObjectPath());for(const auto& A:D->Attacks)Paths.AddUnique(A.Animation.ToSoftObjectPath());}
 FString BindingError;
 const auto& Bindings=AetherMotionBindings::All(BindingError);
 if(!BindingError.IsEmpty()){bFailed=true;UE_LOG(LogTemp,Error,TEXT("AETHER_MOTION_BINDINGS_INVALID %s"),*BindingError);return;}
 for(const auto& B:Bindings)for(const auto& Path:B.RuntimeAssets())Paths.AddUnique(Path);
 Paths.RemoveAll([](const auto& P){return P.IsNull();});
 SharedHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths);
}
bool UAetherAssetPreload::Ready() const
{
 if(!bRequested||bFailed||!SharedHandle||!SharedHandle->HasLoadCompleted())return false;
 TArray<UObject*> Assets;SharedHandle->GetLoadedAssets(Assets);return !Assets.IsEmpty()&&!Assets.Contains(nullptr);
}
void UAetherAssetPreload::Deinitialize()
{
 if(SharedHandle){SharedHandle->CancelHandle();SharedHandle->ReleaseHandle();SharedHandle.Reset();}
 if(RootHandle){RootHandle->CancelHandle();RootHandle->ReleaseHandle();RootHandle.Reset();}
 Content=nullptr;Super::Deinitialize();
}
