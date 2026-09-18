#pragma once
#include "Profile/AetherProfileState.h"
namespace AetherProfileCodec
{
    inline constexpr uint16 SchemaVersion=1;
    inline constexpr int32 MaxBytes=4*1024*1024;
    AETHERCORE_API bool Encode(const FAetherProfileStateV10& State,const FAetherV10ItemDefinitions& Items,
        const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,TArray<uint8>& Bytes,FString& Reason);
    AETHERCORE_API bool Decode(const TArray<uint8>& Bytes,const FAetherV10ItemDefinitions& Items,
        const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,FAetherProfileStateV10& Out,FString& Reason);
}
