#pragma once
#include "Commands/AetherProfileCommand.h"
#include "World/AetherWorldState.h"
namespace AetherInteractionRewards
{
    EAetherCommandCode Apply(const FAetherInteractionActionDefinition& Action,const FAetherProfileCommandContext& Context,
        FAetherProfileStateV10& Profile,FAetherWorldStateV10& World,const FAetherV10ItemDefinitions& Items,const FAetherRules& Rules);
}
