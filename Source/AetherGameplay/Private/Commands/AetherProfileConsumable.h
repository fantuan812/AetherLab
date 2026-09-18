#pragma once
#include "Commands/AetherProfileCommand.h"
namespace AetherProfileConsumable
{
    EAetherCommandCode Apply(const FAetherPlayerCommand& Command,FAetherProfileStateV10& Next,
        const FAetherProfileCommandContext& Context,const FAetherV10ItemDefinitions& Items,const FAetherRules& Rules,
        FAetherEffectDelivery& Delivery,FAetherCommandResult& Result);
}
