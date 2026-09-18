#pragma once
#include "Commands/AetherProfileCommand.h"
namespace AetherProfileEconomy
{
    EAetherCommandCode Apply(const FAetherPlayerCommand& Command,FAetherProfileStateV10& Candidate,
        const FAetherProfileCommandContext& Context,const FAetherV10ItemDefinitions& Items,
        const FAetherEconomyDefinitionsV10& Economy,FAetherCommandResult& Result);
}
