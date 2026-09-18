#pragma once
#include "Contracts/AetherPlayerCommand.h"
#include "Contracts/AetherTransaction.h"
#include "Profile/AetherProfileState.h"

// 此上下文只由服务器当前状态构造，不能作为客户端 RPC 参数。
// 不含 Actor/UObject 指针；需要现场权限的命令由协调者在发起提交前重新查询。
struct FAetherProfileCommandContext
{
    bool bCanManageInventory=false;
    FAetherSkillRuleContext Skill;
    TArray<FAetherExternalSkillGrant> ExternalSkillGrants;
};
namespace AetherProfileCommands
{
    // 仅计算候选和完整回执，不发布 Profile、不修改 ASC。回执查询必须先于此函数。
    AETHERGAMEPLAY_API bool Prepare(const FAetherPlayerCommand& Command,const FString& ServerCharacterId,
        const FAetherProfileStateV10& Current,const FAetherProfileCommandContext& Context,
        const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,
        FAetherTransaction& Transaction,FAetherCommandResult& Result);
}
