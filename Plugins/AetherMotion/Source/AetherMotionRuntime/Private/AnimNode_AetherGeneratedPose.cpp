#include "AnimNode_AetherGeneratedPose.h"
#include "AetherMotionComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "GameFramework/Actor.h"
void FAnimNode_AetherGeneratedPose::PreUpdate(const UAnimInstance* Instance)
{
    const auto* Owner=Instance?Instance->GetOwningActor():nullptr;
    const auto* Motion=Owner?Owner->FindComponentByClass<UAetherMotionComponent>():nullptr;
    WorldToActor=Owner?Owner->GetActorQuat().Inverse():FQuat::Identity;
    Clip=Motion?Motion->PoseClip():FAetherMotionClipPtr();Frame=Motion?Motion->PoseFrame():3;Basis=Motion?Motion->PoseBasis():FMatrix::Identity;
}
void FAnimNode_AetherGeneratedPose::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{FAnimNode_Base::Initialize_AnyThread(Context);}
void FAnimNode_AetherGeneratedPose::Update_AnyThread(const FAnimationUpdateContext& Context)
{FAnimNode_Base::Update_AnyThread(Context);}
void FAnimNode_AetherGeneratedPose::Evaluate_AnyThread(FPoseContext& Output)
{
    Output.ResetToRefPose();if(!Clip)return;
    TArray<FTransform> Local;if(!Clip->Sample(Frame,Local,Basis))return;
    // agent 在世界方向规划，目标人物组件已随 Actor 转向；根旋转须先转回人物局部。
    if(!Local.IsEmpty())Local[0].SetRotation((WorldToActor*Local[0].GetRotation()).GetNormalized());
    const auto& Bones=Output.Pose.GetBoneContainer();const auto& Reference=Bones.GetReferenceSkeleton();
    // 名称/层级来自模型导出骨架；绝不把 G1 索引直接写入 Manny 索引。
    for(int32 J=0;J<Clip->Skeleton->Names.Num();++J)
    {
        const int32 MeshIndex=Reference.FindBoneIndex(Clip->Skeleton->Names[J]);if(MeshIndex==INDEX_NONE)continue;
        const FCompactPoseBoneIndex Compact=Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
        if(Compact.IsValid())Output.Pose[Compact]=Local[J];
    }
    Output.Pose.NormalizeRotations();
}
