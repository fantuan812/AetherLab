#pragma once
#include "World/AetherContainerState.h"
namespace AetherContainerCodec
{
    inline constexpr uint16 SchemaVersion=1;
    inline constexpr int32 MaxBytes=2*1024*1024+1024;
    AETHERCORE_API bool Encode(const FAetherContainerStateV10& State,const FAetherV10ItemDefinitions& Definitions,TArray<uint8>& Bytes,FString& Reason);
    AETHERCORE_API bool Decode(const TArray<uint8>& Bytes,const FAetherV10ItemDefinitions& Definitions,FAetherContainerStateV10& Out,FString& Reason);
}
