#pragma once
#include "Engine/DataAsset.h"
#include "AetherMotionProfile.generated.h"
class USkeletalMesh;
class UAnimInstance;
class UAetherMotionBoundaryAsset;
class UIKRetargeter;
UCLASS(BlueprintType)
class AETHERMOTIONRUNTIME_API UAetherMotionProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) int32 Revision=1;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USkeletalMesh> SourceMesh;
    UPROPERTY(EditAnywhere) TSoftClassPtr<UAnimInstance> SourceAnimationClass =
        TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/Animation/Motion/ABP_G1MotionSource.ABP_G1MotionSource_C")));
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UIKRetargeter> Retargeter;
    UPROPERTY(EditAnywhere) TMap<FName,FString> Styles;
    UPROPERTY(EditAnywhere) FString SkeletonSha256;
    UPROPERTY(EditAnywhere) TMap<FName,TSoftObjectPtr<UAetherMotionBoundaryAsset>> TransitionBoundaries;
    // 行向量约定：模型 +X 左髋、Y 上、Z 前 -> UE X 前、Y 右、Z 上（镜像变换），作者工具检查正交/往返。
    UPROPERTY(EditAnywhere) FVector SourceX=FVector(0,-1,0);
    UPROPERTY(EditAnywhere) FVector SourceY=FVector(0,0,1);
    UPROPERTY(EditAnywhere) FVector SourceZ=FVector(1,0,0);
    UPROPERTY(EditAnywhere,meta=(ClampMin=".1",ClampMax="2")) float MaxResultAge=.5f;
    UPROPERTY(EditAnywhere,meta=(ClampMin=".05",ClampMax=".5")) float BlendSeconds=.18f;
    UPROPERTY(EditAnywhere,meta=(ClampMin="1",ClampMax="8")) int32 NativeThreads=2;
    FMatrix Basis() const;
    bool Validate(FString& Reason) const;
};
