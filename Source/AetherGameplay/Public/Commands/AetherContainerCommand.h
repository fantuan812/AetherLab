#pragma once
#include "Commands/AetherProfileCommand.h"
#include "Contracts/AetherTransactionalStore.h"
namespace AetherContainerCommands
{
    AETHERGAMEPLAY_API bool Handles(EAetherCommandType Type);
    // 在从存储读取容器私有负载前调用。Context 必须由当前服务器目标/归属注册表解析。
    AETHERGAMEPLAY_API EAetherCommandCode AuthorizeRead(const FAetherPlayerCommand& Command,
        const FAetherProfileCommandContext& Context,FString& ContainerId);
    // Snapshot 中 Profile/World/Container/版本索引必须来自一次 ReadSnapshot；输出为三聚合单事务。
    AETHERGAMEPLAY_API bool Prepare(const FAetherPlayerCommand& Command,const FString& ServerCharacterId,
        const FAetherStoreSnapshotResult& Snapshot,const FAetherProfileCommandContext& Context,
        const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,
        FAetherTransaction& Transaction,FAetherCommandResult& Result);
}
