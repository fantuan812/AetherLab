#pragma once
#include "Commands/AetherServerFactCoordinator.h"
#include "Definitions/AetherV10Definitions.h"
namespace AetherServerRewards
{
    // Applied 是候选结果；协调者取得原事务提交证明前不得向玩家发布。
    EAetherLootClaimOutcome ApplyLoot(const FAetherServerFact& Event,FAetherProfileStateV10& Profile,
        FAetherWorldStateV10& World,const FAetherV10Definitions& Definitions,FString& Reason);
    bool Apply(const FAetherServerFact& Event,FAetherProfileStateV10& Profile,FAetherWorldStateV10& World,
        const FAetherV10Definitions& Definitions,FString& Reason);
}
