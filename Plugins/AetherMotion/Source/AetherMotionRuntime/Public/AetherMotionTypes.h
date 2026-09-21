#pragma once
#include "CoreMinimal.h"

enum class EAetherMotionBackend:uint8 {Traditional,CPU,Vulkan,Automatic};
struct FAetherMotionStamp
{
    FGuid WorldEpoch,PawnEpoch,ActionInstance;
    uint64 AgentGeneration=1,RequestSequence=0,MovementRevision=0,ProfileRevision=0;
    bool SameLifetime(const FAetherMotionStamp& B) const
    {return WorldEpoch==B.WorldEpoch&&PawnEpoch==B.PawnEpoch&&AgentGeneration==B.AgentGeneration;}
    bool SameIntent(const FAetherMotionStamp& B) const
    {return SameLifetime(B)&&ActionInstance==B.ActionInstance&&MovementRevision==B.MovementRevision&&ProfileRevision==B.ProfileRevision;}
};
struct FAetherMotionSkeleton
{
    TArray<FName> Names;
    TArray<int32> Parents;
    TArray<FVector3f> NeutralMeters;
};
struct AETHERMOTIONRUNTIME_API FAetherMotionBoundary
{
    TArray<float> Roots,Rotations; // 恰好 4 × 3 和 4 × 34 × 4，源模型坐标。
    bool IsValid() const;
};
struct FAetherMotionInput
{
    uint64 AgentId=0;
    FAetherMotionStamp Stamp;
    double SimulationTime=0,SubmittedAt=0;
    uint64 AcceptedSequence=0;
    uint32 ConsumedFrameIndex=0;
    FVector3f Movement=FVector3f(0,0,1),Facing=FVector3f(0,0,1);
    float SpeedMeters=0,Priority=0;
    FString Style=TEXT("idle");
    FAetherMotionBoundary Context;
    TOptional<FAetherMotionBoundary> TransitionTarget;
    uint64 Seed=0;
};
struct AETHERMOTIONRUNTIME_API FAetherMotionClip
{
    FAetherMotionStamp Stamp;
    FString NativeRevision;
    double SimulationTime=0,InferenceSeconds=0;
    uint32 Frames=0;
    TArray<float> Roots,Rotations;
    TSharedPtr<const FAetherMotionSkeleton,ESPMode::ThreadSafe> Skeleton;
    // 位置线性、旋转球面插值；调用者不向 Actor 施加生成的水平根位移。
    bool Sample(double Frame,TArray<FTransform>& LocalPose,const FMatrix& Basis) const;
    FAetherMotionBoundary Boundary(uint32 LastFrame) const;
};
using FAetherMotionClipPtr=TSharedPtr<const FAetherMotionClip,ESPMode::ThreadSafe>;
struct FAetherMotionResult
{
    double SubmittedAt=0;
    uint64 AgentId=0;
    FAetherMotionStamp Stamp;
    FAetherMotionClipPtr Clip;
    FString Reason;
};
