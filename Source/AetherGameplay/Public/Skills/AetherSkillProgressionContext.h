#pragma once
#include "CoreMinimal.h"

class AAetherCharacter;
struct FAetherSkillRuleContext;

namespace AetherSkillProgression
{
// Reads existing authoritative deadlines; never starts, clears or persists a timer.
// Only execution flags are filled. Combat, level and service authority stay with the caller.
AETHERGAMEPLAY_API void ResolveExecutionState(const AAetherCharacter& Character,int64 ProfileRevision,FAetherSkillRuleContext& Context);
}
