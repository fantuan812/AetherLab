#pragma once
#include "Persistence/AetherProfile.h"
#include "Profile/AetherProfileState.h"
namespace AetherLegacyV9
{
    // 输入来自显式旧 reader；此函数仍复验冻结 v9 规则，不读盘、不写盘、不修改 Source。
    AETHERGAMEPLAY_API bool ConvertProfile(const FAetherProfile& Source,int32 SourceSchema,const FString& SourceSha256,
        const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,
        FAetherProfileStateV10& Out,FString& Reason);
}
