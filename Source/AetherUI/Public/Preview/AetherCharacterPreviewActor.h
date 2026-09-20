#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AetherEquipmentVisuals.h"
#include "AetherCharacterPreviewActor.generated.h"

class USceneCaptureComponent2D;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class USkeletalMesh;
class UAnimSequence;
class UTextureRenderTarget2D;

UCLASS(Transient,NotBlueprintable)
class AETHERUI_API AAetherCharacterPreviewActor : public AActor
{
    GENERATED_BODY()
public:
    AAetherCharacterPreviewActor();
    FString ApplyAppearance(USkeletalMesh* Mesh,FRotator Rotation,UAnimSequence* Idle,
        const TArray<FAetherEquipmentVisualSpec>& Equipment,UStaticMesh* Fallback);
    void ClearAppearance();
    void RenderFrame(float DeltaSeconds,float Yaw,float Pitch,float Zoom);
    void SetRenderTarget(UTextureRenderTarget2D* Target,bool Transparent);
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> Capture;
private:
    UPROPERTY() TObjectPtr<UStaticMeshComponent> FallbackBody;
    UPROPERTY() TMap<FName,TObjectPtr<UStaticMeshComponent>> Visuals;
    TMap<FName,FAetherEquipmentVisualSpec> Applied;
    TWeakObjectPtr<UAnimSequence> AppliedIdle;
};
