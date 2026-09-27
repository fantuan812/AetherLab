#pragma once
#include "CoreMinimal.h"

namespace AetherDerivedStats
{
    // Growth is a base contribution; GAS gear/passive effects are already aggregated.
    inline float HealthGrowth(int32 Experience)
    {return 5.f * FMath::Clamp(Experience / 200, 0, 4);}

    inline float MaximumHealth(int32 Experience, float AggregatedGearHealth)
    {
        return FMath::Clamp(100.f + HealthGrowth(Experience) +
            (FMath::IsFinite(AggregatedGearHealth) ? AggregatedGearHealth : 0.f), 1.f, 100000.f);
    }
}
