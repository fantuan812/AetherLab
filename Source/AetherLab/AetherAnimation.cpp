#include "AetherAnimation.h"
#include "AetherMotionComponent.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "AetherCombat.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Interaction/AetherWorldActionComponent.h"
#include "AetherContent.h"
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
 FAnimNode_ModifyBone GuardArm;
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
  GuardArm.ComponentPose.SetLinkNode(&RightHand);GuardArm.BoneToModify.BoneName="lowerarm_l";GuardArm.RotationMode=BMM_Additive;GuardArm.RotationSpace=BCS_BoneSpace;GuardArm.Rotation=FRotator(-55,0,20);
  ToLocal.ComponentPose.SetLinkNode(&GuardArm);
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
  GuardArm.Alpha=A->GuardWeight;
  LeftHand.Alpha=RightHand.Alpha=A->HandWeight;
  LeftHand.EffectorLocation=A->HandTargets[0];RightHand.EffectorLocation=A->HandTargets[1];
  LeftHand.JointTargetLocation=A->ElbowTargets[0];RightHand.JointTargetLocation=A->ElbowTargets[1];
 }
};
}
UAetherAnimInstance::UAetherAnimInstance()
{
 static ConstructorHelpers::FObjectFinder<UAetherActionSet> Actions(TEXT("/Game/Animation/Controlled/DA_Actions.DA_Actions"));ActionSet=Actions.Object;
 static ConstructorHelpers::FObjectFinder<UBlendSpace> Move(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run"));Locomotion=Move.Object;
 static ConstructorHelpers::FObjectFinder<UAnimSequence> Jump(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Jump.MM_Jump"));JumpClip=Jump.Object;
 static ConstructorHelpers::FObjectFinder<UAnimSequence> Fall(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop.MM_Fall_Loop"));FallClip=Fall.Object;
 static ConstructorHelpers::FObjectFinder<UAnimSequence> Land(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Land.MM_Land"));LandClip=Land.Object;
 static ConstructorHelpers::FObjectFinder<UAnimSequence> Heavy(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_ChargedAttack.MM_ChargedAttack"));HeavyClip=Heavy.Object;
 static ConstructorHelpers::FObjectFinder<UAnimSequence> One(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01"));
 static ConstructorHelpers::FObjectFinder<UAnimSequence> Two(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_02.MM_Attack_02"));
 static ConstructorHelpers::FObjectFinder<UAnimSequence> Three(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_03.MM_Attack_03"));LightClips={One.Object,Two.Object,Three.Object};
}
FAnimInstanceProxy* UAetherAnimInstance::CreateAnimInstanceProxy(){return new FAetherAnimProxy(this);}
void UAetherAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy){delete Proxy;}
void UAetherAnimInstance::NativeInitializeAnimation(){Super::NativeInitializeAnimation();SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);}
EAetherMotionState UAetherAnimInstance::SelectState(bool Alive,bool Stunned,bool Falling,float VerticalSpeed,bool Landing,bool Guarding)
{
 if(!Alive)return EAetherMotionState::Downed;if(Stunned)return EAetherMotionState::Stunned;if(Falling)return VerticalSpeed>60?EAetherMotionState::Rising:EAetherMotionState::Falling;
 return Landing?EAetherMotionState::Landing:Guarding?EAetherMotionState::Guarding:EAetherMotionState::Grounded;
}
float UAetherAnimInstance::AttackPosition(const FAetherAttackDefinition& A,float T,float Length)
{
 T=FMath::Clamp(T,0.f,A.Duration());float Position=0;
 if(T<A.WindupSeconds)Position=.35f*T/FMath::Max(A.WindupSeconds,SMALL_NUMBER);
 else if(T<A.WindupSeconds+A.ActiveSeconds)Position=.35f+.3f*(T-A.WindupSeconds)/FMath::Max(A.ActiveSeconds,SMALL_NUMBER);
 else Position=.65f+.35f*(T-A.WindupSeconds-A.ActiveSeconds)/FMath::Max(A.RecoverySeconds,SMALL_NUMBER);
 return FMath::Clamp(Position,0.f,1.f)*FMath::Max(Length,0.f);
}
void UAetherAnimInstance::NativeUpdateAnimation(float Dt)
{
 Super::NativeUpdateAnimation(Dt);auto* C=Cast<AAetherCharacter>(TryGetPawnOwner());if(!C||C->GetNetMode()==NM_DedicatedServer)return;
 const float Now=C->CombatTime();const bool Falling=C->GetCharacterMovement()->IsFalling();
 if(WasFalling&&!Falling){LandingId=LastVerticalSpeed<-700?TEXT("LandHeavy"):TEXT("Land");LandUntil=Now+(LastVerticalSpeed<-700?.45f:.18f);}
 LastVerticalSpeed=C->GetVelocity().Z;WasFalling=Falling;
 if(WasAlive&&!C->Alive())DownedAt=Now;
 if(!WasAlive&&C->Alive())RevivedAt=Now;
 WasAlive=C->Alive();
 GroundSpeed=C->GetVelocity().Size2D();const FVector V=C->GetActorTransform().InverseTransformVectorNoScale(C->GetVelocity());Direction=FMath::RadiansToDegrees(FMath::Atan2(V.Y,V.X));
 MotionState=SelectState(C->Alive(),Now<C->StunUntil,Falling,C->GetVelocity().Z,Now<LandUntil,C->bBlocking);
 const bool Air=MotionState==EAetherMotionState::Rising||MotionState==EAetherMotionState::Falling;
 AirWeight=FMath::FInterpTo(AirWeight,Air?1.f:0.f,Dt,14);
 // 控制动作按服务器时钟取样，晚加入/短暂不可见不会从动画第一帧重新播放。
 FName Id;float Elapsed=0,Duration=0;bool Loop=false;
 const auto* Player=Cast<AAetherFrontierCharacter>(C);
 if(!C->Alive()){Id=TEXT("Death");Elapsed=Now-DownedAt;}
 else if(Now<C->StunUntil){Id=TEXT("Stun");Elapsed=Now;Loop=true;}
 else if(Now<RevivedAt+1.2f){Id=TEXT("GetUp");Elapsed=Now-RevivedAt;Duration=1.2f;}
 else if(C->PresentedAction.Duration>0&&Now<C->PresentedAction.StartedAt+C->PresentedAction.Duration)
 {Id=C->PresentedAction.Id;Elapsed=Now-C->PresentedAction.StartedAt;Duration=C->PresentedAction.Duration;}
 else if(Player&&Player->ReviveTarget){Id=TEXT("Rescue");Elapsed=Now;Loop=true;}
 else if(Player&&Player->Carried){Id=GroundSpeed>10?TEXT("CarryWalk"):TEXT("CarryIdle");Elapsed=Now;Loop=true;}
 else if(Now<C->CastLockUntil){Id=TEXT("Cast");Elapsed=Now-C->CastStartedAt;Duration=FMath::Max(.1f,C->CastLockUntil-C->CastStartedAt);}
 else if(C->bBlocking){Id=TEXT("Guard");Elapsed=Now;Loop=true;}
 else if(Now<LandUntil){Id=LandingId;Elapsed=Now-(LandUntil-(LandingId==TEXT("LandHeavy")?.45f:.18f));}
 else if(C->IsCrouched())
 {
   Id=GroundSpeed<10?TEXT("CrouchIdle"):FMath::Abs(Direction)>135?TEXT("CrouchBack"):
       Direction>45?TEXT("CrouchRight"):Direction<-45?TEXT("CrouchLeft"):TEXT("CrouchWalk");
   Elapsed=Now;Loop=true;
 }
 UAnimSequence* Selected=nullptr;if(ActionSet)if(const auto* Found=ActionSet->Clips.Find(Id))Selected=Found->Get();
 if(Selected)
 {
   ControlledClip=Selected;const float Length=Selected->GetPlayLength();
   ControlledTime=Loop?FMath::Fmod(FMath::Max(0.f,Elapsed),FMath::Max(.01f,Length)):
       FMath::Clamp(Duration>0?Elapsed/Duration*Length:Elapsed,0.f,Length);
 }
 ControlledWeight=FMath::FInterpTo(ControlledWeight,Selected?1.f:0.f,Dt,18);
 if(!Selected&&ControlledWeight<.001f)ControlledClip=nullptr;
 // 格挡使用完整受控动画，取消旧的单骨骼抬臂替代。
 GuardWeight=0;
 const bool Busy=C->Equipment->IsBusy()&&C->Alive()&&Now>=C->StunUntil;
 if(Busy)
 {
  const auto* Attack=C->Equipment->CurrentAttack();
  if(Attack&&LastAttack!=C->Equipment->Attack.Serial)
  {
   LastAttack=C->Equipment->Attack.Serial;
   UAnimSequence* Clip=C->Equipment->Attack.AttackId=="Heavy"?HeavyClip.Get():LightClips[LastAttack%LightClips.Num()].Get();
   if(Attack->Animation.IsValid())Clip=Attack->Animation.Get();else if(!Attack->Animation.IsNull())Clip=Attack->Animation.LoadSynchronous();
   if(Clip){AttackMontage=PlaySlotAnimationAsDynamicMontage(Clip,"DefaultSlot",.06f,.12f);if(AttackMontage)Montage_SetPlayRate(AttackMontage,0);}
  }
  if(Attack&&AttackMontage)Montage_SetPosition(AttackMontage,AttackPosition(*Attack,C->Equipment->Clock()-C->Equipment->Attack.StartedAt,AttackMontage->GetPlayLength()));
 }
 else if(AttackMontage){Montage_Stop(.12f,AttackMontage);AttackMontage=nullptr;}
 const bool SolveFeet=!Air&&!Busy&&C->Alive()&&Now>=C->StunUntil&&
   (Id.IsNone()||Id.ToString().StartsWith(TEXT("Crouch"))||Id==TEXT("Guard")||Id.ToString().StartsWith(TEXT("Carry")))&&
   GroundSpeed<230&&C->GetMesh()->WasRecentlyRendered(.2f);
 if(SolveFeet&&Now>=TraceAt)
 {
  TraceAt=Now+.05f;bool Both=true;auto* Mesh=C->GetMesh();FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherFootIK),false,C);
  for(int I=0;I<2;++I)
  {
   FVector Foot=Mesh->GetSocketLocation(I==0?FName("foot_l"):FName("foot_r"));const float Floor=C->GetActorLocation().Z-C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
   FVector Base(Foot.X,Foot.Y,Floor);FHitResult Hit;
   const bool Valid=GetWorld()->LineTraceSingleByChannel(Hit,Base+FVector(0,0,35),Base-FVector(0,0,35),ECC_Visibility,Q)&&Hit.ImpactNormal.Z>.6f;
   Both&=Valid;if(Valid){Foot.Z=FMath::Clamp(Hit.ImpactPoint.Z+12.,double(Floor-18),double(Floor+18));FootTargets[I]=Mesh->GetComponentTransform().InverseTransformPosition(Foot);KneeTargets[I]=Mesh->GetComponentTransform().InverseTransformPosition(C->GetActorLocation()+C->GetActorForwardVector()*100+C->GetActorRightVector()*(I==0?-20:20));}
  }
  bFeetValid=Both;
 }
 FootWeight=FMath::FInterpTo(FootWeight,SolveFeet&&bFeetValid?1.f:0.f,Dt,10);
 // 手部接触来自当前服务器占用目标；只解双臂，不移动胶囊或世界物体。
 bool ContactValid=false;FVector Contact=FVector::ZeroVector;float HalfWidth=22;
 if(Player&&C->Alive()&&Now>=C->StunUntil)
 {
   if(AActor* Target=Player->WorldActions?Player->WorldActions->ContactActor():nullptr)
   {
     FVector Extent;Target->GetActorBounds(true,Contact,Extent);
     HalfWidth=FMath::Clamp(float(Extent.Size2D()*.5),12.f,35.f);ContactValid=true;
   }
   else if(Player->ReviveTarget){Contact=Player->ReviveTarget->GetActorLocation()+FVector(0,0,15);ContactValid=true;HalfWidth=16;}
 }
 if(C->PresentedAction.bHasContact&&C->PresentedAction.Id==TEXT("Vault")&&Now<C->PresentedAction.StartedAt+.6f)
 {Contact=C->PresentedAction.Contact;ContactValid=true;HalfWidth=22;}
 auto* Mesh=C->GetMesh();
 ContactValid=ContactValid&&FVector::DistSquared(Contact,Mesh->GetSocketLocation(TEXT("spine_03")))<FMath::Square(140.);
 if(ContactValid)
 {
   for(int32 I=0;I<2;++I)
   {
     const float Sign=I==0?-1.f:1.f;
     HandTargets[I]=Mesh->GetComponentTransform().InverseTransformPosition(Contact+C->GetActorRightVector()*(Sign*HalfWidth));
     ElbowTargets[I]=Mesh->GetComponentTransform().InverseTransformPosition(C->GetActorLocation()+C->GetActorForwardVector()*30+C->GetActorRightVector()*(Sign*80)+FVector(0,0,25));
   }
 }
 HandWeight=FMath::FInterpTo(HandWeight,ContactValid?1.f:0.f,Dt,12);
}
