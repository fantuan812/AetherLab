#include "../AetherFrontier.h"
#include "../AetherAnimation.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"
void AAetherFrontierMode::CheckAnimation()
{
 auto* C=Cast<AAetherFrontierCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
 if(!C){if(Elapsed>8)FPlatformMisc::RequestExitWithStatus(false,1);return;}
 auto* M=C->GetMesh();auto* A=Cast<UAetherAnimInstance>(M->GetAnimInstance());bool Pass=A&&A->Locomotion&&A->JumpClip&&A->FallClip&&A->LandClip&&A->HeavyClip&&A->LightClips.Num()==3;
 if(A)
 {
  M->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
  auto Evaluate=[&](){M->TickAnimation(.05f,false);M->RefreshBoneTransforms();for(const auto& T:M->GetComponentSpaceTransforms())Pass&=!T.ContainsNaN();};
  Evaluate();Pass&=M->GetComponentSpaceTransforms().Num()>50;
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
