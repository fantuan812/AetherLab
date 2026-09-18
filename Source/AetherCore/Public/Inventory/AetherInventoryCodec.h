#pragma once
#include "Inventory/AetherInventoryState.h"

// 持久 DTO 的编码版本与内容定义版本、网络协议版本独立；不使用 SerializeBin。
// 编码先验证全部值，解码只在完整校验后替换输出，失败保留调用方已有状态。
namespace AetherInventoryCodec
{
    inline constexpr uint16 SchemaVersion=1;
    inline constexpr int32 MaxBytes=2*1024*1024;
    AETHERCORE_API bool Encode(const FAetherInventoryStateV10& State,const FAetherV10ItemDefinitions& Definitions,TArray<uint8>& Bytes,FString& Reason);
    AETHERCORE_API bool Decode(const TArray<uint8>& Bytes,const FAetherV10ItemDefinitions& Definitions,FAetherInventoryStateV10& State,FString& Reason);
}
