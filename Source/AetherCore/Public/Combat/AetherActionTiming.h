#pragma once
#include "CoreMinimal.h"
// Authoritative timing version 1; animation sampling never decides effect commits.
namespace AetherActionTiming
{
    inline constexpr float AttackCharge=.35f,AttackBuffer=.12f;
    inline constexpr float PushDuration=.7f,PushContact=.35f,PushImpulse=15000.f;
    inline constexpr float PickupDuration=.7f,HandContact=.35f,ReleaseDuration=.7f;
    inline constexpr float DodgeDuration=.55f,DodgeMotion=.35f,DodgeSpeed=700.f,DodgeCost=18.f,DodgeCooldown=.7f,DodgeInvulnerability=.22f;
    inline constexpr float VaultRise=.22f,VaultAcross=.38f,VaultLand=.22f,VaultDuration=VaultRise+VaultAcross+VaultLand;
}
