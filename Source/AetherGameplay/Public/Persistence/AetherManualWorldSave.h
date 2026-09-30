#pragma once
#include "Persistence/AetherWorldCheckpoint.h"

// A request stays pending until durable confirmation; admission is not success.
class FAetherManualWorldSave
{
public:
    bool IsPending() const{return Pending.IsValid();}
    bool Start(TFunctionRef<TFuture<FAetherWorldCheckpointResult>()> Save)
    {
        if(IsPending())return false;
        Pending=Save();return true;
    }
    TOptional<FString> Poll()
    {
        if(!Pending.IsValid()||!Pending.IsReady())return {};
        const auto R=Pending.Get();Pending={};
        if((R.Code==EAetherStoreCode::Committed||R.Code==EAetherStoreCode::Replayed)&&R.World.IsSet())
            return FString::Printf(TEXT("世界检查点已保存（修订 %lld）；角色变更由事务即时保存。"),R.World->Revision);
        if(R.Code==EAetherStoreCode::Busy)
            return FString(TEXT("保存暂忙，请稍后按 F5 重试。"));
        return FString(TEXT("保存未确认，请稍后按 F5 重试；不要据此退出。"));
    }
private:
    TFuture<FAetherWorldCheckpointResult> Pending;
};
