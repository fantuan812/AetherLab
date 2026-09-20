#pragma once
#include "Commands/AetherServerFactCoordinator.h"
#include "Definitions/AetherV10Definitions.h"
namespace AetherServerRewards
{
    bool Apply(const FAetherServerFact& Event,FAetherProfileStateV10& Profile,FAetherWorldStateV10& World,
        const FAetherV10Definitions& Definitions,FString& Reason);
}
