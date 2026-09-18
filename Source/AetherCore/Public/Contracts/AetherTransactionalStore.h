#pragma once
#include "Contracts/AetherTransaction.h"
#include "Async/Future.h"

struct FAetherStoreReadResult
{
    EAetherStoreCode Code = EAetherStoreCode::Unavailable;
    TOptional<FAetherStoredAggregate> Value;
    FString Detail;
};
struct FAetherStoreEffectsResult
{
    EAetherStoreCode Code = EAetherStoreCode::Unavailable;
    TArray<FAetherEffectDelivery> Values;
    FString Detail;
};

// 单写者拥有数据库连接。输入都是值对象，后台线程不得捕获 Actor/UObject 裸指针。
// Future 只表示持久提交结果；协调者回到游戏线程并复核会话 epoch 后，才能发布复制状态。
class AETHERCORE_API IAetherTransactionalStore
{
public:
    IAetherTransactionalStore();
    virtual ~IAetherTransactionalStore();
    virtual TFuture<FAetherStoreResult> Commit(FAetherTransaction Transaction) = 0;
    virtual TFuture<FAetherStoreReadResult> Read(FAetherAggregateKey Key) = 0;
    virtual TFuture<FAetherStoreEffectsResult> PendingEffects(FString ActorId) = 0;
    virtual TFuture<bool> AcknowledgeEffect(FString ActorId, FGuid DeliveryId) = 0;
    virtual TFuture<bool> Backup(FString Destination) = 0;
    // 停止接收新任务，等待已入队事务结束，再关闭连接；析构必须调用。
    virtual void Close() = 0;
};
