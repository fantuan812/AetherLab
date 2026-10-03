#include "Animation/AetherAnimation.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
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
#include "AetherMotionBinding.h"
#include "Engine/SkeletalMesh.h"
UAetherAnimInstance::UAetherAnimInstance() = default;
void UAetherAnimInstance::NativeInitializeAnimation(){Super::NativeInitializeAnimation();SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);}
void UAetherAnimInstance::LoadBoundAnimations()
{
 check(IsInGameThread());
 ActionSet=nullptr;Locomotion=nullptr;JumpClip=nullptr;FallClip=nullptr;LandClip=nullptr;HeavyClip=nullptr;LightClips.Reset();
 MainHandBone=NAME_None;SupportHandBone=NAME_None;SupportGrip=FAetherResolvedWeaponGrip();
 const auto* Mesh=GetSkelMeshComponent()?GetSkelMeshComponent()->GetSkeletalMeshAsset():nullptr;
 FString Why;const auto* B=AetherMotionBindings::ForMesh(FSoftObjectPath(Mesh),Why);
 if(!B){UE_LOG(LogTemp,Error,TEXT("AETHER_ANIMATION_BINDING_UNAVAILABLE %s"),*Why);return;}
 const auto* Right=B->Chains.FindByPredicate([](const auto& Chain){return Chain.Name==TEXT("RightWrist");});
 const auto* Left=B->Chains.FindByPredicate([](const auto& Chain){return Chain.Name==TEXT("LeftWrist");});
 if(!Right||!Left||Right->TargetEnd==Left->TargetEnd||Mesh->GetRefSkeleton().FindBoneIndex(Right->TargetEnd)==INDEX_NONE||
    Mesh->GetRefSkeleton().FindBoneIndex(Left->TargetEnd)==INDEX_NONE)
 {UE_LOG(LogTemp,Error,TEXT("AETHER_ANIMATION_RESOURCES_INVALID %s: wrist chains unavailable"),*B->Id);return;}
 MainHandBone=Right->TargetEnd;SupportHandBone=Left->TargetEnd;
 auto Get=[&](const TCHAR* Key){const auto* Path=B->AnimationAssets.Find(FName(Key));return Path?Path->TryLoad():nullptr;};
 auto* Actions=Cast<UAetherActionSet>(Get(TEXT("actions")));auto* Move=Cast<UBlendSpace>(Get(TEXT("locomotion")));
 auto* Jump=Cast<UAnimSequence>(Get(TEXT("jump")));auto* Fall=Cast<UAnimSequence>(Get(TEXT("fall")));
 auto* Land=Cast<UAnimSequence>(Get(TEXT("land")));auto* Heavy=Cast<UAnimSequence>(Get(TEXT("heavy")));
 TArray<TObjectPtr<UAnimSequence>> Light;for(const auto& Path:B->LightAnimations)Light.Add(Cast<UAnimSequence>(Path.TryLoad()));
 auto Matches=[&](const UAnimSequence* Clip){return Clip&&Clip->GetSkeleton()==Mesh->GetSkeleton();};
 bool Valid=Actions&&Actions->bDefinitionValid&&Move&&Move->GetSkeleton()==Mesh->GetSkeleton()&&Matches(Jump)&&Matches(Fall)&&Matches(Land)&&Matches(Heavy)&&!Light.IsEmpty();
 for(const auto& Clip:Light)Valid&=Matches(Clip);
 if(Actions)for(const auto& Pair:Actions->Clips)Valid&=Matches(Pair.Value);
 if(!Valid){UE_LOG(LogTemp,Error,TEXT("AETHER_ANIMATION_RESOURCES_INVALID %s: missing resource or skeleton mismatch"),*B->Id);return;}
 ActionSet=Actions;Locomotion=Move;JumpClip=Jump;FallClip=Fall;LandClip=Land;HeavyClip=Heavy;LightClips=MoveTemp(Light);
}
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
    const double Started=FPlatformTime::Seconds();
    Super::NativeUpdateAnimation(Dt);
    auto* C=Cast<AAetherCharacter>(TryGetPawnOwner());if(!C||C->GetNetMode()==NM_DedicatedServer||!Locomotion||LightClips.IsEmpty())return;
    ON_SCOPE_EXIT{if(C->Motion)C->Motion->RecordBridgeSeconds(FPlatformTime::Seconds()-Started);};
    // 在游戏线程只采样一次；图代理只消费本帧数值，独立负责传统/生成姿态混合。
    const auto Frame=AetherAnimationSnapshot::Capture(*C);
    UpdateLocomotion(Frame,Dt);
    UpdateActions(*C,Frame,Dt);
    UpdateContacts(*C,Frame,Dt);
}
void UAetherAnimInstance::UpdateLocomotion(const FAetherAnimationSnapshot& Frame,float Dt)
{
 const float Now=Frame.Time;const bool Falling=Frame.bFalling;
 if(WasFalling&&!Falling)
 {
  LandingId=LastVerticalSpeed<-700?TEXT("LandHeavy"):TEXT("Land");LandStarted=Now;
  const auto* Rule=AetherControlledActions::Find(LandingId);LandUntil=Now+(Rule?Rule->Duration:0);
 }
 LastVerticalSpeed=Frame.Velocity.Z;WasFalling=Falling;
 if(WasAlive&&!Frame.bAlive)DownedAt=Now;
 if(!WasAlive&&Frame.bAlive)RevivedAt=Now;
 WasAlive=Frame.bAlive;
 GroundSpeed=Frame.Velocity.Size2D();Direction=Frame.Direction;
 MotionState=SelectState(Frame.bAlive,Frame.bStunned,Falling,Frame.Velocity.Z,Now<LandUntil,Frame.bBlocking);
 const bool Air=MotionState==EAetherMotionState::Rising||MotionState==EAetherMotionState::Falling;
 AirWeight=FMath::FInterpTo(AirWeight,Air?1.f:0.f,Dt,14);
}
