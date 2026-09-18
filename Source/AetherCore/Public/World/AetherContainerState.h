#pragma once
#include "Inventory/AetherInventoryState.h"
enum class EAetherContainerKind:uint8 {SharedChest,PersonalStorage,WorldDrop};
struct AETHERCORE_API FAetherContainerStateV10
{
    FString ContainerId;
    int64 Revision=0;
    EAetherContainerKind Kind=EAetherContainerKind::SharedChest;
    FString OwnerCharacterId,RegionId;
    FVector Location=FVector::ZeroVector;
    // 空掉落留下非活动墓碑，保留并发版本；静态箱子/个人仓储为空时仍保持活动。
    bool bActive=true;
    FAetherInventoryStateV10 Inventory;
    bool Validate(const FAetherV10ItemDefinitions& Definitions,FString& Reason) const;
    // 仅持久归属检查；目标加载、距离、视线、安全收纳和会话有效性仍由服务器现场复验。
    bool Allows(const FString& ServerCharacterId) const;
};
