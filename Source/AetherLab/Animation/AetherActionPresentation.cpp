#include "AetherCombat.h"
void AAetherCharacter::PresentAction(FName Id,float Duration)
{
    if(!HasAuthority()&&!IsLocallyControlled())return;
    if(Id.IsNone()||!FMath::IsFinite(Duration)||Duration<=0||Duration>10)return;
    PresentedAction.bHasContact=false;PresentedAction.Contact=FVector::ZeroVector;
    PresentedAction.Id=Id;PresentedAction.StartedAt=CombatTime();PresentedAction.Duration=Duration;
    ++PresentedAction.Serial;if(HasAuthority())ForceNetUpdate();
}
