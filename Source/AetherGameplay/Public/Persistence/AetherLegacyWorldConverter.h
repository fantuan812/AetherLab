#pragma once
#include "Persistence/AetherFrontierSave.h"
#include "World/AetherWorldState.h"
#include "Profile/AetherProfileState.h"
#include "Contracts/AetherTransactionalStore.h"
namespace AetherLegacyV9
{
    AETHERGAMEPLAY_API bool ConvertWorld(const UAetherFrontierSave& Source,const FString& SourceSha256,
        const FAetherV10ItemDefinitions& Items,const FAetherRules& Rules,const TMap<FString,int64>& ProfileRevisions,
        FAetherWorldStateV10& Out,FString& Reason);
    // 纯转换：所有角色、世界及跨引用成功后一次发布导入值；不接触数据库或在线 Actor。
    AETHERGAMEPLAY_API bool ConvertSnapshot(const UAetherFrontierSave& Source,const FString& SourceSha256,
        const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,
        FAetherLegacyImport& Out,FString& Reason);
}
