#pragma once
#include "World/AetherWorldState.h"
namespace AetherWorldCodec
{
    inline constexpr uint16 SchemaVersion=1;
    inline constexpr int32 MaxBytes=4*1024*1024;
    AETHERCORE_API bool Encode(const FAetherWorldStateV10& State,const FAetherV10ItemDefinitions& Items,
        const FAetherRules& Rules,const TMap<FString,int64>& Profiles,TArray<uint8>& Bytes,FString& Reason);
    AETHERCORE_API bool Decode(const TArray<uint8>& Bytes,const FAetherV10ItemDefinitions& Items,
        const FAetherRules& Rules,const TMap<FString,int64>& Profiles,FAetherWorldStateV10& Out,FString& Reason);
}
