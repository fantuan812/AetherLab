#pragma once
#include "Camera/CameraActor.h"
#include "AetherDialogueCameraActor.generated.h"
class APlayerCameraManager;
// Local display resource. A surrendered outgoing view survives its session until the engine releases both references.
UCLASS(NotPlaceable,Transient)
class AETHERGAMEPLAY_API AAetherDialogueCameraActor : public ACameraActor
{
    GENERATED_BODY()
public:
    AAetherDialogueCameraActor();
    void RetireWhenUnreferenced(APlayerCameraManager* Manager);
    virtual void Tick(float DeltaSeconds) override;
private:
    TWeakObjectPtr<APlayerCameraManager> RetiringManager;
    bool bRetiring=false;
};
