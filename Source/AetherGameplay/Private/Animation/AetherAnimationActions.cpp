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
void UAetherAnimInstance::UpdateActions(AAetherCharacter& Character,const FAetherAnimationSnapshot& Frame,float Dt)
{
 auto* C=&Character;const float Now=Frame.Time;
 // 控制动作按服务器时钟取样，晚加入/短暂不可见不会从动画第一帧重新播放。
 FName Id;float Elapsed=0,Duration=0;bool Loop=false;
 if(!Frame.bAlive){Id=TEXT("Death");Elapsed=Now-DownedAt;}
 else if(Frame.bStunned){Id=TEXT("Stun");Elapsed=Now;Loop=true;}
 else if(Now<RevivedAt+1.2f){Id=TEXT("GetUp");Elapsed=Now-RevivedAt;Duration=1.2f;}
 else if(Frame.Action.Duration>0&&Now<Frame.Action.StartedAt+Frame.Action.Duration)
 {Id=Frame.Action.Id;Elapsed=Now-Frame.Action.StartedAt;Duration=Frame.Action.Duration;}
 else if(Frame.bRescuing){Id=TEXT("Rescue");Elapsed=Now;Loop=true;}
 else if(Frame.bCarrying){Id=GroundSpeed>10?TEXT("CarryWalk"):TEXT("CarryIdle");Elapsed=Now;Loop=true;}
 else if(Now<Frame.CastUntil){Id=TEXT("Cast");Elapsed=Now-Frame.CastStarted;Duration=FMath::Max(.1f,Frame.CastUntil-Frame.CastStarted);}
 else if(Frame.bBlocking){Id=TEXT("Guard");Elapsed=Now;Loop=true;}
 else if(Now<LandUntil){Id=LandingId;Elapsed=Now-(LandUntil-(LandingId==TEXT("LandHeavy")?.45f:.18f));}
 else if(Frame.bCrouched)
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
 const float GeneratedCrouch=Frame.bCrouched&&Id.ToString().StartsWith(TEXT("Crouch"))&&C->Motion?C->Motion->GeneratedWeight():0.f;
 ControlledWeight=FMath::FInterpTo(ControlledWeight,Selected?1.f-GeneratedCrouch:0.f,Dt,18);
 if(!Selected&&ControlledWeight<.001f)ControlledClip=nullptr;
 // 格挡使用完整受控动画，取消旧的单骨骼抬臂替代。
 ControlledActionId=Id;const bool Busy=Frame.bAttacking;
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
}
