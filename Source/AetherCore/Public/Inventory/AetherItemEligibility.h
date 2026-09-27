#pragma once
#include "Inventory/AetherInventoryState.h"
#include "Definitions/AetherRules.h"

enum class EAetherUseAvailability : uint8 { Allowed, Unknown, Restricted, Cooldown, Unsafe, NoBenefit };
struct FAetherUseSummary
{
    bool bKnown=false,bCanAct=false;
    double Health=0,Mana=0,Stamina=0,MaxHealth=0,MaxMana=0,MaxStamina=0;
    double CooldownRemaining=0,SafeForSeconds=0;
};

enum class EAetherItemOperation : uint8 { PersonalStorage, SharedStorage, Withdraw, Sell, Drop, Use };

namespace AetherItemEligibility
{
    // Pure query shared by presentation and authority. Live authorization stays on the server.
    AETHERCORE_API EAetherInventoryMutationCode Query(const FAetherInventoryStateV10& Inventory,
        FGuid Id, const FString& Owner, EAetherItemOperation Operation, const FAetherV10ItemDefinitions& Definitions);
    AETHERCORE_API EAetherUseAvailability QueryUse(const FAetherUseRule& Rule,const FAetherUseSummary& Summary);
    AETHERCORE_API FString UseReason(EAetherUseAvailability Result);
}
