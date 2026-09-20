#pragma once
#include "Inventory/AetherEconomyDefinitions.h"
#include "Skills/AetherSkillDefinitions.h"
#include "Interaction/AetherInteractionDefinitions.h"
#include "Quests/AetherQuestProgression.h"

// 运行时与网络解码共用这一份规范定义；不在 UI、RPC 或存储各自维护不同的价格/技能参数。
struct AETHERCORE_API FAetherV10Definitions
{
    FAetherV10ItemDefinitions Items;
    FAetherSkillDefinitionsV10 Skills;
    FAetherRules Rules;
    FAetherEconomyDefinitionsV10 Economy;
    FAetherInteractionDefinitions Interactions;
    FAetherQuestProgressionDefinitions Progression;
    bool bValid=false;
    FString Error;
    static const FAetherV10Definitions& Get();
};
