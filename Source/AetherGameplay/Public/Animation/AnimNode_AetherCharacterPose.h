#pragma once
#include "Animation/AnimNodeBase.h"
#include "AnimNode_AetherCharacterPose.generated.h"

// 正式 AnimBP 的姿态入口。原生代理拥有图节点；此节点把完整生命周期转发给该图。
// GetCustomRootNode 只适用于非蓝图实例，不能靠空 AnimBP 自动调用它。
USTRUCT(BlueprintInternalUseOnly)
struct AETHERGAMEPLAY_API FAnimNode_AetherCharacterPose : public FAnimNode_Base
{
    GENERATED_BODY()
    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
private:
    FAnimNode_Base* NativeRoot=nullptr;
    bool bReported=false;
};
