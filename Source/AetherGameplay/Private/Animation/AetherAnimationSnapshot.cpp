#include "Animation/AetherAnimationSnapshot.h"
#include "Combat/AetherCombat.h"
#include "Characters/AetherFrontierCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
FAetherAnimationSnapshot AetherAnimationSnapshot::Capture(const AAetherCharacter& C)
{
    check(IsInGameThread());FAetherAnimationSnapshot S;
    S.Time=C.CombatTime();S.Velocity=C.GetVelocity();S.bAlive=C.Alive();S.bStunned=S.Time<C.StunUntil;
    S.bFalling=C.GetCharacterMovement()->IsFalling();S.bCrouched=C.IsCrouched();S.bBlocking=C.bBlocking;
    S.bAttacking=C.Equipment&&C.Equipment->IsBusy()&&S.bAlive&&!S.bStunned;
    S.CastStarted=C.CastStartedAt;S.CastUntil=C.CastLockUntil;S.Action=C.PresentedAction;
    const FVector Local=C.GetActorTransform().InverseTransformVectorNoScale(S.Velocity);
    S.Direction=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
    if(const auto* P=Cast<AAetherFrontierCharacter>(&C)){S.bCarrying=P->Carried!=nullptr;S.bRescuing=P->ReviveTarget!=nullptr;}
    return S;
}
