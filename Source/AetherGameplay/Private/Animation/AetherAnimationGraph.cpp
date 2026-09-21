#include "Animation/AetherAnimation.h"
#include "Animation/AnimNode_AetherCharacterPose.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "AetherMotionComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
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
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "BoneControllers/AnimNode_TwoBoneIK.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
namespace
{
// 从本次未经 IK 的足部姿态计算目标，避免上一帧已落地的 socket 反馈把摆动脚永久压平。
// 摆动期保留作者/生成姿态；接地期只在有界范围内锁定足点，地形修正保留抬脚量。
struct FAetherGroundFootIK : FAnimNode_TwoBoneIK
{
 double FlatContactZ=12;
 bool Planted=false;FVector PlantAnchor=FVector::ZeroVector,PreviousOrigin=FVector::ZeroVector;
 double CorrectiveStepAge=-1;FVector StepStart=FVector::ZeroVector;
 FTransform BaseTransform=FTransform::Identity;TWeakObjectPtr<UPrimitiveComponent> MovementBase;
 virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Out) override
 {
  const auto Index=IKBone.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer());
  const FVector Authored=Output.Pose.GetComponentSpaceTransform(Index).GetLocation();
  const FVector Surface=EffectorLocation;
  EffectorLocation=FVector(Authored.X,Authored.Y,FMath::Max(Surface.Z,Authored.Z+Surface.Z-FlatContactZ));
  const FTransform Component=Output.AnimInstanceProxy->GetComponentTransform();
  if(FVector::DistSquared(Component.GetLocation(),PreviousOrigin)>FMath::Square(100.)){Planted=false;CorrectiveStepAge=-1;}
  PreviousOrigin=Component.GetLocation();
  // 模型不保证刚性接触。接地期在移动基座局部锁住足点，摆腿时立即释放；
  // 超过有界步幅则释放，不能拉伸腿去追传送前或受阻时的陈旧锚点。
  const bool Contact=Authored.Z<=FlatContactZ+(Planted?7.:3.);
  const FVector DesiredWorld=Component.TransformPosition(EffectorLocation);
  if(!Contact){Planted=false;CorrectiveStepAge=-1;}
  else if(!Planted){PlantAnchor=BaseTransform.InverseTransformPosition(DesiredWorld);Planted=true;}
  if(Planted){
   const FVector AnchorWorld=BaseTransform.TransformPosition(PlantAnchor);
   if(FVector::Dist2D(DesiredWorld,AnchorWorld)>45&&CorrectiveStepAge<0){CorrectiveStepAge=0;StepStart=PlantAnchor;}
   if(CorrectiveStepAge>=0){
    // 超步幅时补一个有抬脚和落脚的短步，禁止接地状态瞬移锚点产生可见滑步。
    CorrectiveStepAge+=Output.AnimInstanceProxy->GetDeltaSeconds();
    const double T=FMath::Clamp(CorrectiveStepAge/.18,0.,1.);
    FVector StepWorld=FMath::Lerp(BaseTransform.TransformPosition(StepStart),DesiredWorld,T);
    StepWorld.Z+=16*FMath::Sin(PI*T);EffectorLocation=Component.InverseTransformPosition(StepWorld);
    if(T>=1){PlantAnchor=BaseTransform.InverseTransformPosition(DesiredWorld);CorrectiveStepAge=-1;}
   }else {const FVector Anchor=Component.InverseTransformPosition(AnchorWorld);EffectorLocation.X=Anchor.X;EffectorLocation.Y=Anchor.Y;}
  }
  FAnimNode_TwoBoneIK::EvaluateSkeletalControl_AnyThread(Output,Out);
  EffectorLocation=Surface;
 }
};
struct FAetherSupportHandIK : FAnimNode_TwoBoneIK
{
 FBoneReference MainHand;FVector GripOffset=FVector::ZeroVector;bool UseGrip=false;
 virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override
 {FAnimNode_TwoBoneIK::CacheBones_AnyThread(Context);MainHand.BoneName=TEXT("hand_r");MainHand.Initialize(Context.AnimInstanceProxy->GetRequiredBones());}
 virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Out) override
 {
  const FVector Contact=EffectorLocation;
  if(UseGrip&&MainHand.IsValidToEvaluate(Output.Pose.GetPose().GetBoneContainer()))
   EffectorLocation=Output.Pose.GetComponentSpaceTransform(MainHand.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer())).TransformPosition(GripOffset);
  FAnimNode_TwoBoneIK::EvaluateSkeletalControl_AnyThread(Output,Out);EffectorLocation=Contact;
 }
};
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
 FAnimNode_SequencePlayer_Standalone GripPose[2];
 FAnimNode_LayeredBoneBlend GripBlend[2];
 FAnimNode_ConvertLocalToComponentSpace ToComponent;
 FAetherGroundFootIK LeftFoot,RightFoot;
 FAetherSupportHandIK LeftHand;
 FAnimNode_TwoBoneIK RightHand;
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
  // 官方徒手动作中的闭合手指姿态只覆盖十组指骨，不覆盖手腕、手臂或生成的身体动作。
  for(int32 Side=0;Side<2;++Side){
   GripPose[Side].SetSequence(A->LightClips.Num()?A->LightClips[0].Get():nullptr);
   GripPose[Side].SetPlayRate(0);GripPose[Side].SetLoopAnimation(false);GripPose[Side].SetAccumulatedTime(.3f);
   auto& Layer=GripBlend[Side];Layer=FAnimNode_LayeredBoneBlend();Layer.AddPose();Layer.BlendWeights[0]=0;
   Layer.BasePose.SetLinkNode(Side?static_cast<FAnimNode_Base*>(&GripBlend[0]):static_cast<FAnimNode_Base*>(&ControlledBlend));
   Layer.BlendPoses[0].SetLinkNode(&GripPose[Side]);
   for(const TCHAR* Finger:{TEXT("thumb"),TEXT("index"),TEXT("middle"),TEXT("ring"),TEXT("pinky")}){
    FBranchFilter Filter;Filter.BoneName=FName(*FString::Printf(TEXT("%s_01_%s"),Finger,Side?TEXT("r"):TEXT("l")));Filter.BlendDepth=0;
    Layer.LayerSetup[0].BranchFilters.Add(Filter);
   }
  }
  ToComponent.LocalPose.SetLinkNode(&GripBlend[1]);LeftFoot.ComponentPose.SetLinkNode(&ToComponent);RightFoot.ComponentPose.SetLinkNode(&LeftFoot);
  RightHand.ComponentPose.SetLinkNode(&RightFoot);LeftHand.ComponentPose.SetLinkNode(&RightHand);
  ToLocal.ComponentPose.SetLinkNode(&LeftHand);
  for(FAnimNode_TwoBoneIK* Foot:{static_cast<FAnimNode_TwoBoneIK*>(&LeftFoot),static_cast<FAnimNode_TwoBoneIK*>(&RightFoot),static_cast<FAnimNode_TwoBoneIK*>(&LeftHand),&RightHand}){Foot->EffectorLocationSpace=BCS_ComponentSpace;Foot->JointTargetLocationSpace=BCS_ComponentSpace;Foot->bAllowStretching=false;Foot->bMaintainEffectorRelRot=true;Foot->Alpha=0;}
  LeftFoot.IKBone.BoneName="foot_l";RightFoot.IKBone.BoneName="foot_r";
  LeftHand.IKBone.BoneName="hand_l";RightHand.IKBone.BoneName="hand_r";
  FAnimInstanceProxy::Initialize(Instance);
 }
 virtual void PreUpdate(UAnimInstance* Instance,float Dt) override
 {
  const double Started=FPlatformTime::Seconds();
  FAnimInstanceProxy::PreUpdate(Instance,Dt);const auto* A=CastChecked<UAetherAnimInstance>(Instance);
  auto* C=Cast<AAetherCharacter>(Instance->TryGetPawnOwner());auto* Motion=C?C->Motion.Get():nullptr;
  ON_SCOPE_EXIT{if(Motion)Motion->RecordBridgeSeconds(FPlatformTime::Seconds()-Started);};
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
  if(C){
   const FVector FlatWorld=C->GetActorLocation()-FVector(0,0,C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-12);
   LeftFoot.FlatContactZ=RightFoot.FlatContactZ=C->GetMesh()->GetComponentTransform().InverseTransformPosition(FlatWorld).Z;
   auto* Base=C->GetMovementBase();
   for(auto* Foot:{&LeftFoot,&RightFoot}){
    if(Foot->MovementBase.Get()!=Base||A->FootWeight<.05f)Foot->Planted=false;
    Foot->MovementBase=Base;Foot->BaseTransform=Base?Base->GetComponentTransform():FTransform::Identity;
   }
  }
  LeftFoot.Alpha=RightFoot.Alpha=A->FootWeight;LeftFoot.EffectorLocation=A->FootTargets[0];RightFoot.EffectorLocation=A->FootTargets[1];LeftFoot.JointTargetLocation=A->KneeTargets[0];RightFoot.JointTargetLocation=A->KneeTargets[1];
  for(int32 I=0;I<2;++I)GripBlend[I].BlendWeights[0]=A->GripWeights[I];
  LeftHand.UseGrip=A->HandWeight<.01f&&A->SupportHandWeight>.01f;LeftHand.GripOffset=A->SupportHandOffset;
  LeftHand.Alpha=LeftHand.UseGrip?A->SupportHandWeight:A->HandWeight;RightHand.Alpha=A->HandWeight;
  LeftHand.EffectorLocation=A->HandTargets[0];RightHand.EffectorLocation=A->HandTargets[1];
  LeftHand.JointTargetLocation=A->ElbowTargets[0];RightHand.JointTargetLocation=A->ElbowTargets[1];
  if(A->HandWeight<.01f&&A->WeaponHoldWeight>.01f){
   RightHand.Alpha=A->WeaponHoldWeight;RightHand.EffectorLocation=A->WeaponHoldTarget;RightHand.JointTargetLocation=A->WeaponElbowTarget;
  }
 }
};
}
FAnimInstanceProxy* UAetherAnimInstance::CreateAnimInstanceProxy(){return new FAetherAnimProxy(this);}
void UAetherAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy){delete Proxy;}

void FAnimNode_AetherCharacterPose::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
 FAnimNode_Base::Initialize_AnyThread(Context);
 // 蓝图必须继承人物动画类，避免把其他代理解释为人物代理。
 NativeRoot=Cast<UAetherAnimInstance>(Context.AnimInstanceProxy->GetAnimInstanceObject())
  ?static_cast<FAetherAnimProxy*>(Context.AnimInstanceProxy)->GetCustomRootNode():nullptr;
 if(NativeRoot)NativeRoot->Initialize_AnyThread(Context);
}
void FAnimNode_AetherCharacterPose::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
 if(NativeRoot)NativeRoot->CacheBones_AnyThread(Context);
}
void FAnimNode_AetherCharacterPose::Update_AnyThread(const FAnimationUpdateContext& Context)
{
 if(NativeRoot)NativeRoot->Update_AnyThread(Context);
}
void FAnimNode_AetherCharacterPose::Evaluate_AnyThread(FPoseContext& Output)
{
 if(NativeRoot)NativeRoot->Evaluate_AnyThread(Output);else Output.ResetToRefPose();
#if !UE_BUILD_SHIPPING
 if(!bReported&&FParse::Param(FCommandLine::Get(),TEXT("AetherAnimationCheck")))
 {
  bReported=true;auto* Proxy=static_cast<FAetherAnimProxy*>(Output.AnimInstanceProxy);
  UE_LOG(LogTemp,Display,TEXT("AETHER_ANIMATION_GRAPH root=%d ground=%s bones=%d"),NativeRoot!=nullptr,*GetNameSafe(Proxy->Ground.GetBlendSpace()),Output.Pose.GetNumBones());
 }
#endif
}
