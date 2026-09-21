#pragma once
#include "Engine/DataAsset.h"
#include "AetherMotionTypes.h"
#include "AetherMotionBoundaryAsset.generated.h"
/** 离线作者产生的四帧模型坐标边界；只作为姿态过渡目标，不能驱动角色碰撞位移。 */
UCLASS(BlueprintType)
class AETHERMOTIONRUNTIME_API UAetherMotionBoundaryAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) FString SkeletonSha256;
    UPROPERTY(EditAnywhere) FString NativeRevision;
    UPROPERTY(EditAnywhere) TArray<float> Roots;
    UPROPERTY(EditAnywhere) TArray<float> Rotations;
    bool Read(const FString& ExpectedSkeleton,FAetherMotionBoundary& Out) const;
};
