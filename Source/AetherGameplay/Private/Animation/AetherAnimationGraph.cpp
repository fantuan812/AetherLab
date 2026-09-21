#include "Animation/AetherAnimation.h"
#include "AetherMotionComponent.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "Combat/AetherCombat.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Interaction/AetherWorldActionComponent.h"
#include "Assets/AetherContent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "BoneControllers/AnimNode_TwoBoneIK.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
namespace
{
struct FAetherAnimProxy : FAnimInstanceProxy
{
 FAnimNode_BlendSpacePlayer_Standalone Ground;
 FAnimNode_SequencePlayer_Standalone Air;
 FAnimNode_RetargetPoseFromMesh Generated;
 FAnimNode_TwoWayBlend GeneratedBlend;
 FAnimNode_TwoWayBlend Travel;
 FAnimNode_Slot Action;
 FAnimNode_SequencePlayer_Standalone Controlled;
 FAnimNode_TwoWayBlend ControlledBlend;
 FAnimNode_ConvertLocalToComponentSpace ToComponent;
 FAnimNode_TwoBoneIK LeftFoot,RightFoot,LeftHand,RightHand;
 FAnimNode_ConvertComponentToLocalSpace ToLocal;
 EAetherMotionState Previous=EAetherMotionState::Grounded;
 explicit FAetherAnimProxy(UAnimInstance* Instance):FAnimInstanceProxy(Instance){}
 virtual FAnimNode_Base* GetCustomRootNode() override {return &ToLocal;}
 virtual void Initialize(UAnimInstance* Instance) override
 {
  auto* A=CastChecked<UAetherAnimInstance>(Instance);Ground.SetBlendSpace(A->Locomotion);Air.SetSequence(A->FallClip);
  Generated.RetargetFrom=ERetargetSourceMode::CustomSkeletalMeshComponent;
  GeneratedBlend.A.SetLinkNode(&Ground);GeneratedBlend.B.SetLinkNode(&Generated);GeneratedBlend.Alpha=0;
  Travel.A.SetLinkNode(&GeneratedBlend);Travel.B.SetLinkNode(&Air);Action.Source.SetLinkNode(&Travel);Action.SlotName="DefaultSlot";Action.bAlwaysUpdateSourcePose=true;
  ControlledBlend.A.SetLinkNode(&Action);ControlledBlend.B.SetLinkNode(&Controlled);ControlledBlend.Alpha=0;
  ToComponent.LocalPose.SetLinkNode(&ControlledBlend);LeftFoot.ComponentPose.SetLinkNode(&ToComponent);RightFoot.ComponentPose.SetLinkNode(&LeftFoot);
  LeftHand.ComponentPose.SetLinkNode(&RightFoot);RightHand.ComponentPose.SetLinkNode(&LeftHand);
  ToLocal.ComponentPose.SetLinkNode(&RightHand);
  for(auto* Foot:{&LeftFoot,&RightFoot,&LeftHand,&RightHand}){Foot->EffectorLocationSpace=BCS_ComponentSpace;Foot->JointTargetLocationSpace=BCS_ComponentSpace;Foot->bAllowStretching=false;Foot->bMaintainEffectorRelRot=true;Foot->Alpha=0;}
  LeftFoot.IKBone.BoneName="foot_l";RightFoot.IKBone.BoneName="foot_r";
  LeftHand.IKBone.BoneName="hand_l";RightHand.IKBone.BoneName="hand_r";
  FAnimInstanceProxy::Initialize(Instance);
 }
 virtual void PreUpdate(UAnimInstance* Instance,float Dt) override
 {
  FAnimInstanceProxy::PreUpdate(Instance,Dt);const auto* A=CastChecked<UAetherAnimInstance>(Instance);
  auto* C=Cast<AAetherCharacter>(Instance->TryGetPawnOwner());auto* Motion=C?C->Motion.Get():nullptr;
  Generated.SourceMeshComponent=Motion?Motion->GetSourceMesh():nullptr;Generated.IKRetargeterAsset=Motion?Motion->GetRetargeter():nullptr;
  GeneratedBlend.Alpha=Motion&&Generated.SourceMeshComponent.IsValid()&&Generated.IKRetargeterAsset?Motion->GeneratedWeight():0.f;
  if(Generated.IKRetargeterAsset&&Generated.SourceMeshComponent.IsValid())Generated.PreUpdate(Instance);
  FVector Position(A->GroundSpeed,0,0);
  if(A->Locomotion){for(int I=0;I<2;++I){const auto& Param=A->Locomotion->GetBlendParameter(I);if(Param.DisplayName.Contains(TEXT("Direction")))Position[I]=A->Direction;else if(I==0||Param.DisplayName.Contains(TEXT("Speed")))Position[I]=A->GroundSpeed;}}
  Ground.SetPosition(Position);Ground.SetPlayRate(FMath::Clamp(A->GroundSpeed/450.f,1.f,1.4f));Travel.Alpha=A->AirWeight;
  UAnimSequence* Clip=A->MotionState==EAetherMotionState::Rising?A->JumpClip.Get():A->MotionState==EAetherMotionState::Falling?A->FallClip.Get():A->LandClip.Get();
  if(Air.GetSequence()!=Clip||Previous!=A->MotionState){Air.SetSequence(Clip);Air.SetAccumulatedTime(0);}
  Air.SetPlayRate(1.f);Air.SetLoopAnimation(A->MotionState==EAetherMotionState::Falling);
  Controlled.SetSequence(A->ControlledClip);Controlled.SetPlayRate(0);Controlled.SetLoopAnimation(false);
  Controlled.SetAccumulatedTime(A->ControlledTime);ControlledBlend.Alpha=A->ControlledClip?A->ControlledWeight:0;
  Previous=A->MotionState;
  LeftFoot.Alpha=RightFoot.Alpha=A->FootWeight;LeftFoot.EffectorLocation=A->FootTargets[0];RightFoot.EffectorLocation=A->FootTargets[1];LeftFoot.JointTargetLocation=A->KneeTargets[0];RightFoot.JointTargetLocation=A->KneeTargets[1];
  LeftHand.Alpha=RightHand.Alpha=A->HandWeight;
  LeftHand.EffectorLocation=A->HandTargets[0];RightHand.EffectorLocation=A->HandTargets[1];
  LeftHand.JointTargetLocation=A->ElbowTargets[0];RightHand.JointTargetLocation=A->ElbowTargets[1];
 }
};
}
FAnimInstanceProxy* UAetherAnimInstance::CreateAnimInstanceProxy(){return new FAetherAnimProxy(this);}
void UAetherAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy){delete Proxy;}
