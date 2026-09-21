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
void UAetherAnimInstance::UpdateContacts(AAetherCharacter& Character,const FAetherAnimationSnapshot& Frame,float Dt)
{
 auto* C=&Character;const auto* Player=Cast<AAetherFrontierCharacter>(C);const float Now=Frame.Time;
 const bool Air=Frame.bFalling,Busy=Frame.bAttacking;const FName Id=ControlledActionId;
 const bool SolveFeet=!Air&&!Busy&&Frame.bAlive&&!Frame.bStunned&&
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
 if(Player&&Frame.bAlive&&!Frame.bStunned)
 {
   if(AActor* Target=Player->WorldActions?Player->WorldActions->ContactActor():nullptr)
   {
     FVector Extent;Target->GetActorBounds(true,Contact,Extent);
     HalfWidth=FMath::Clamp(float(Extent.Size2D()*.5),12.f,35.f);ContactValid=true;
   }
   else if(Player->ReviveTarget){Contact=Player->ReviveTarget->GetActorLocation()+FVector(0,0,15);ContactValid=true;HalfWidth=16;}
 }
 if(Frame.Action.bHasContact&&Frame.Action.Id==TEXT("Vault")&&Now<Frame.Action.StartedAt+.6f)
 {Contact=Frame.Action.Contact;ContactValid=true;HalfWidth=22;}
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
