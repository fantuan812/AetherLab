#pragma once
#include "Skills/AetherSkillDefinitions.h"

// Frozen before Commit: callbacks must not replace the identity/rank/cost under a live stack.
struct FAetherCastExecution
{
    FGuid ExecutionId=FGuid::NewGuid(), LifeId;
    FGuid TargetLifeId;
    FString SkillId;
    int32 Rank=0, DefinitionRevision=0;
    EAetherSkillMechanic Mechanic=EAetherSkillMechanic::Fire;
    FAetherSkillRankEffect Effect;
    bool bCostApplied=false, bResultCommitted=false, bRefunded=false;
};
