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
    Super::NativeUpdateAnimation(Dt);
    auto* C=Cast<AAetherCharacter>(TryGetPawnOwner());if(!C||C->GetNetMode()==NM_DedicatedServer)return;
    // 在游戏线程只采样一次；图代理只消费本帧数值，独立负责传统/生成姿态混合。
    const auto Frame=AetherAnimationSnapshot::Capture(*C);
    UpdateLocomotion(Frame,Dt);
    UpdateActions(*C,Frame,Dt);
    UpdateContacts(*C,Frame,Dt);
}
void UAetherAnimInstance::UpdateLocomotion(const FAetherAnimationSnapshot& Frame,float Dt)
{
 const float Now=Frame.Time;const bool Falling=Frame.bFalling;
 if(WasFalling&&!Falling){LandingId=LastVerticalSpeed<-700?TEXT("LandHeavy"):TEXT("Land");LandUntil=Now+(LastVerticalSpeed<-700?.45f:.18f);}
 LastVerticalSpeed=Frame.Velocity.Z;WasFalling=Falling;
 if(WasAlive&&!Frame.bAlive)DownedAt=Now;
 if(!WasAlive&&Frame.bAlive)RevivedAt=Now;
 WasAlive=Frame.bAlive;
 GroundSpeed=Frame.Velocity.Size2D();Direction=Frame.Direction;
 MotionState=SelectState(Frame.bAlive,Frame.bStunned,Falling,Frame.Velocity.Z,Now<LandUntil,Frame.bBlocking);
 const bool Air=MotionState==EAetherMotionState::Rising||MotionState==EAetherMotionState::Falling;
 AirWeight=FMath::FInterpTo(AirWeight,Air?1.f:0.f,Dt,14);
}
