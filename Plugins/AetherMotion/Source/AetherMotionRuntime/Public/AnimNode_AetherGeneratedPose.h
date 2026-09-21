#pragma once
#include "Animation/AnimNodeBase.h"
#include "AetherMotionTypes.h"
#include "AnimNode_AetherGeneratedPose.generated.h"
USTRUCT(BlueprintInternalUseOnly)
struct AETHERMOTIONRUNTIME_API FAnimNode_AetherGeneratedPose : public FAnimNode_Base
{
    GENERATED_BODY()
    virtual bool HasPreUpdate() const override{return true;}
    virtual void PreUpdate(const UAnimInstance* Instance) override;
    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
private:
    FAetherMotionClipPtr Clip;
    double Frame=3;
    FMatrix Basis=FMatrix::Identity;
    FQuat WorldToActor=FQuat::Identity;
};
