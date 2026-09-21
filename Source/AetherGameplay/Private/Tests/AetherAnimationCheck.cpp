#include "Framework/AetherFrontier.h"
#include "Animation/AetherAnimation.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"
void AAetherFrontierMode::CheckAnimation()
{
 auto* C=Cast<AAetherFrontierCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
 if(!C){if(Elapsed>8)FPlatformMisc::RequestExitWithStatus(false,1);return;}
 auto* M=C->GetMesh();
 if(!M->GetSkeletalMeshAsset()||!M->GetAnimInstance()||!C->Ready()||C->bTravelPending){if(Elapsed>60)FPlatformMisc::RequestExitWithStatus(false,1);return;}
 // 此回归固定传统路径，另由真模型图测试验证生成源和重定向。
 if(auto* Backend=IConsoleManager::Get().FindConsoleVariable(TEXT("aether.Motion.Backend")))Backend->Set(0,ECVF_SetByConsole);
 auto* A=Cast<UAetherAnimInstance>(M->GetAnimInstance());bool Pass=A&&A->Locomotion&&A->JumpClip&&A->FallClip&&A->LandClip&&A->HeavyClip&&A->LightClips.Num()==3;
 if(A)
 {
  M->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
  auto Evaluate=[&](){M->TickAnimation(.05f,false);M->RefreshBoneTransforms();for(const auto& T:M->GetComponentSpaceTransforms())Pass&=!T.ContainsNaN();};
  Evaluate();Pass&=M->GetComponentSpaceTransforms().Num()>50;
  Pass&=A->GetClass()->GetPathName().StartsWith(TEXT("/Game/Animation/ABP_AetherCharacter"));
  const auto& Ref=M->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose();
  const auto& Actual=M->GetBoneSpaceTransforms();int32 NonReference=0;
  for(int32 I=1;I<FMath::Min(Ref.Num(),Actual.Num());++I)
   if(!Actual[I].GetRotation().Equals(Ref[I].GetRotation(),.01f)||!Actual[I].GetTranslation().Equals(Ref[I].GetTranslation(),.5f))++NonReference;
  Pass&=NonReference>=8;
  UE_LOG(LogTemp,Display,TEXT("AETHER_ANIMATION_AUTHORED nonReferenceBones=%d class=%s"),NonReference,*A->GetClass()->GetPathName());
  const auto Before=M->GetBoneSpaceTransforms();
  for(int32 I=0;I<5;++I)Evaluate();
  int32 Changed=0;const auto& After=M->GetBoneSpaceTransforms();
  for(int32 I=1;I<FMath::Min(Before.Num(),After.Num());++I)
   if(!Before[I].GetRotation().Equals(After[I].GetRotation(),.0001f))++Changed;
  Pass&=Changed>=3;
  UE_LOG(LogTemp,Display,TEXT("AETHER_ANIMATION_TIME changedBones=%d"),Changed);
  C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);C->GetCharacterMovement()->Velocity.Z=300;Evaluate();Pass&=A->MotionState==EAetherMotionState::Rising;
  C->GetCharacterMovement()->Velocity.Z=-300;Evaluate();Pass&=A->MotionState==EAetherMotionState::Falling;
  C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);C->GetCharacterMovement()->Velocity=FVector::ZeroVector;Evaluate();Pass&=A->MotionState==EAetherMotionState::Landing;
  if(auto* Montage=A->PlaySlotAnimationAsDynamicMontage(A->HeavyClip,"DefaultSlot",.06f,.12f)){A->Montage_SetPosition(Montage,.2f);Evaluate();}else Pass=false;
  FAetherAttackDefinition Attack;const float Start=UAetherAnimInstance::AttackPosition(Attack,Attack.WindupSeconds,1),End=UAetherAnimInstance::AttackPosition(Attack,Attack.WindupSeconds+Attack.ActiveSeconds,1);
  Pass&=FMath::IsNearlyEqual(Start,.35f)&&FMath::IsNearlyEqual(End,.65f)&&UAetherAnimInstance::AttackPosition(Attack,Attack.Duration()+1,1)==1;
  Pass&=UAetherAnimInstance::SelectState(false,true,true,100,true,true)==EAetherMotionState::Downed;
 }
 UE_LOG(LogTemp,Display,TEXT("AETHER_ANIMATION_%s bones=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),M->GetComponentSpaceTransforms().Num());
 FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
