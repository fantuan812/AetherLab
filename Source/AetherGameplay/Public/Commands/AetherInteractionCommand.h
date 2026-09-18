#pragma once
#include "Commands/AetherProfileCommand.h"
#include "Contracts/AetherTransactionalStore.h"
#include "Quests/AetherQuestProgression.h"
namespace AetherInteractionCommands
{
    AETHERGAMEPLAY_API EAetherCommandCode AuthorizeRead(const FAetherPlayerCommand& Command,const FString& Actor,const FAetherProfileCommandContext& Context);
    // 首批迁移：登记、据点绑定、故事能力授予、任务结算和任务技能点领取。
    // 休息/训练生成/交易会话等尚无对应副作用适配器，必须返回 UnsupportedAction。
    AETHERGAMEPLAY_API bool Prepare(const FAetherPlayerCommand& Command,const FString& Actor,const FAetherStoreSnapshotResult& Snapshot,
        const FAetherProfileCommandContext& Context,const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,
        const FAetherRules& Rules,const FAetherEconomyDefinitionsV10& Economy,const FAetherInteractionDefinitions& Definitions,
        const FAetherQuestProgressionDefinitions& Progression,FAetherTransaction& Transaction,FAetherCommandResult& Result);
}
