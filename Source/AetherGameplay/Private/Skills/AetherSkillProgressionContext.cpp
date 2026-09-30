#include "Skills/AetherSkillProgressionContext.h"
#include "Combat/AetherCombat.h"
#include "Framework/AetherProgression.h"
#include "Effects/AetherBuffRuntime.h"
#include "Skills/AetherSkillState.h"
#include "Engine/World.h"

void AetherSkillProgression::ResolveExecutionState(const AAetherCharacter& Character,int64 ProfileRevision,FAetherSkillRuleContext& Context)
{
    const double Now=Character.CombatTime();
    Context.bCasting=Character.CastLockUntil>Now;
    // A missing owner or unsynchronized presentation cannot declare progression safe.
    Context.bCoolingDown=true;
    const auto* PlayerState=Character.GetPlayerState<AAetherPlayerState>();
    if(!PlayerState||!FMath::IsFinite(Now))return;
    if(Character.HasAuthority())
    {
        Context.bCoolingDown=PlayerState->SkillCooldowns.ContainsByPredicate(
            [Now](const FAetherSkillCooldownDeadline& Deadline){return Deadline.EndsAt>Now;});
        return;
    }
    const auto* Effects=Character.BuffRuntime.Get();
    if(!Character.GetWorld()->GetGameState()||!Effects||!Effects->PresentationReady(ProfileRevision))return;
    // Owner-only atomic presentation is copied from the same PlayerState deadlines.
    // Expiration is evaluated against synchronized server time, never the local wall clock.
    Context.bCoolingDown=false;
    for(const auto& Deadline:Effects->Snapshot.Cooldowns)
        if(Deadline.Value>Now){Context.bCoolingDown=true;break;}
}
