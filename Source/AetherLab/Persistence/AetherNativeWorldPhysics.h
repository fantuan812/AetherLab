#pragma once
#include "World/AetherWorldState.h"
#include "ReactiveTypes.h"
class UWorld;

// 物理快照是原生世界聚合的投影；不借旧 SaveGame 存储一份新的可写权威。
namespace AetherNativeWorldPhysics
{
    AETHERLAB_API FAetherReactiveRecordV10 ToNativeRecord(const FReactiveSaveRecord& Record);
    AETHERLAB_API FReactiveSaveRecord ToRuntimeRecord(const FAetherReactiveRecordV10& Record);
    AETHERLAB_API bool RestoreLoaded(UWorld& World,const FAetherWorldStateV10& Snapshot,FString& Reason,bool Partial=false);
    // 只替换已加载实体，离线区域保留冻结状态；候选随后仍需通过原生世界事务提交。
    AETHERLAB_API bool CaptureLoaded(UWorld& World,const FAetherWorldStateV10& Previous,FAetherWorldStateV10& Candidate,FString& Reason);
}
