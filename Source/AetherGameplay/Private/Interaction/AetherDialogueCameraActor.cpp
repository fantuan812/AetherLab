#include "Interaction/AetherDialogueCameraActor.h"
#include "Camera/PlayerCameraManager.h"
AAetherDialogueCameraActor::AAetherDialogueCameraActor()
{
    bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bStartWithTickEnabled=false;
}
void AAetherDialogueCameraActor::RetireWhenUnreferenced(APlayerCameraManager* Manager)
{RetiringManager=Manager;bRetiring=true;SetActorTickEnabled(true);Tick(0);}
void AAetherDialogueCameraActor::Tick(float Dt)
{
    Super::Tick(Dt);if(!bRetiring)return;
    const auto* Manager=RetiringManager.Get();
    if(!Manager||Manager->IsActorBeingDestroyed()||(Manager->ViewTarget.Target!=this&&Manager->PendingViewTarget.Target!=this))Destroy();
}
