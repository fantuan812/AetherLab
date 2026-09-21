#pragma once
#include "World/AetherWorldState.h"
#include "ReactiveTypes.h"
class UWorld;

// 物理快照是原生世界聚合的投影；不借旧 SaveGame 存储一份新的可写权威。
namespace AetherNativeWorldPhysics
{
    AETHERGAMEPLAY_API FAetherReactiveRecordV10 ToNativeRecord(const FReactiveSaveRecord& Record);
    AETHERGAMEPLAY_API FReactiveSaveRecord ToRuntimeRecord(const FAetherReactiveRecordV10& Record);
    AETHERGAMEPLAY_API bool RestoreLoaded(UWorld& World,const FAetherWorldStateV10& Snapshot,FString& Reason,bool Partial=false);
    // 只替换已加载实体，离线区域保留冻结状态；候选随后仍需通过原生世界事务提交。
    AETHERGAMEPLAY_API bool CaptureLoaded(UWorld& World,const FAetherWorldStateV10& Previous,FAetherWorldStateV10& Candidate,FString& Reason);
}
