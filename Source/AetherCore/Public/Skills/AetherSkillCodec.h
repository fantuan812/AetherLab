#pragma once
#include "Skills/AetherSkillState.h"
namespace AetherSkillCodec
{
    inline constexpr uint16 SchemaVersion=1;
    inline constexpr int32 MaxBytes=1024*1024;
    AETHERCORE_API bool Encode(const FAetherSkillStateV10& State,const FAetherSkillDefinitionsV10& Definitions,TArray<uint8>& Bytes,FString& Reason);
    // 有界显式格式；任何解码失败都保留 Out 原值，不能清空后覆盖合法存档。
    AETHERCORE_API bool Decode(const TArray<uint8>& Bytes,const FAetherSkillDefinitionsV10& Definitions,FAetherSkillStateV10& Out,FString& Reason);
}
