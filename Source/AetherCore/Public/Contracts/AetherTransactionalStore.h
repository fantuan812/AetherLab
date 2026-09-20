#pragma once
#include "Contracts/AetherTransaction.h"
#include "Async/Future.h"
#include "World/AetherContainerState.h"

// 仅查询完整命令身份对应的持久回执；Missing 不是执行许可，提交时仍原子复验版本与回执。
struct FAetherReceiptQuery
{
    FString ActorId;
    FGuid CommandId;
    int32 ProtocolVersion=1;
    TArray<uint8> Request;
};
struct FAetherStoreReadResult
{
    EAetherStoreCode Code = EAetherStoreCode::Unavailable;
    TOptional<FAetherStoredAggregate> Value;
    FString Detail;
};
// 启动恢复/跨域验证使用的有界索引；不含 Profile/Container 私有负载，不能直接作为客户端列表。
struct FAetherStoreRevisionIndex
{
    EAetherStoreCode Code=EAetherStoreCode::Unavailable;
    TMap<FString,int64> Revisions;
    FString Detail;
};
struct FAetherStoreSnapshotQuery
{
    TArray<FAetherAggregateKey> Keys;
    bool bIncludeProfileRevisions=false;
    bool bIncludeContainerCount=false;
    bool bIncludeContainerRevisions=false;
    bool bIncludeWorldRevisions=false;
};
struct FAetherStoreSnapshotResult
{
    EAetherStoreCode Code=EAetherStoreCode::Unavailable;
    // 不存在的 key 不加入 Values，便于区分新掉落创建与已有容器；其他错误整个读取失败。
    TMap<FAetherAggregateKey,FAetherStoredAggregate> Values;
    TMap<FString,int64> ProfileRevisions;
    int32 ContainerCount=-1;
    TMap<FString,int64> ContainerRevisions;
    TMap<FString,int64> WorldRevisions;
    FString Detail;
};
struct FAetherStoreEffectsResult
{
    EAetherStoreCode Code = EAetherStoreCode::Unavailable;
    TArray<FAetherEffectDelivery> Values;
    FString Detail;
};

// 完成全部领域转换/不变量校验后才能提交；保留旧版本，不能拆成若干普通玩家事务。
struct FAetherLegacyImport
{
    int32 SourceSchema=5;
    FString SourceSha256;
    TArray<FAetherStoredAggregate> Values;
};
namespace AetherImports
{
    inline constexpr int32 MaxValues=129; // 128 个角色 + 1 个完整世界聚合。
    inline constexpr int32 MaxBytes=32*1024*1024;
    AETHERCORE_API bool Validate(const FAetherLegacyImport& Import,FString& Reason);
}
// 单写者拥有数据库连接。输入都是值对象，后台线程不得捕获 Actor/UObject 裸指针。
// Future 只表示持久提交结果；协调者回到游戏线程并复核会话 epoch 后，才能发布复制状态。
class AETHERCORE_API IAetherTransactionalStore
{
public:
    IAetherTransactionalStore();
    virtual ~IAetherTransactionalStore();
    virtual TFuture<FAetherStoreResult> Commit(FAetherTransaction Transaction) = 0;
    virtual TFuture<FAetherStoreResult> LookupReceipt(FAetherReceiptQuery Query) = 0;
    // 仅接受空数据库或同一已导入来源的重试；不能覆盖已有游戏数据。
    virtual TFuture<FAetherStoreResult> ImportLegacy(FAetherLegacyImport Import) = 0;
    // 服务器启动/登录专用，不能从玩家协议调用。仅插入不存在的记录，不推进或覆盖已有版本。
    // 默认失败使尚未实现该能力的替换后端明确拒绝启动，而不是降级为普通 Commit。
    virtual TFuture<FAetherStoreResult> InitializeWorld(FAetherStoredAggregate World);
    virtual TFuture<FAetherStoreReadResult> CreateProfile(FAetherStoredAggregate Profile);
    // 服务器装配专用：只创建空的静态箱子/个人仓储，不能借此生成物品或覆盖已有容器。
    virtual TFuture<FAetherStoreReadResult> CreateEmptyContainer(FAetherContainerStateV10 Container,FAetherV10ItemDefinitions Definitions);
    // 服务器世界检查点专用。仅更新 Main 已有行，期望版本必须匹配；不伪造玩家命令或推进任意角色。
    // 返回确认后的世界行；同一目标版本及逐字节相同负载允许安全重试。玩家奖励仍走跨聚合 Commit。
    virtual TFuture<FAetherStoreReadResult> CompareExchangeWorld(FAetherAggregateWrite Write);
    virtual TFuture<FAetherStoreReadResult> Read(FAetherAggregateKey Key) = 0;
    virtual TFuture<FAetherStoreRevisionIndex> ReadRevisions(EAetherAggregateKind Kind) = 0;
    // 多聚合及相关索引共享同一个 SQLite 读事务，不能拼接不同提交时刻的数据。
    virtual TFuture<FAetherStoreSnapshotResult> ReadSnapshot(FAetherStoreSnapshotQuery Query) = 0;
    virtual TFuture<FAetherStoreEffectsResult> PendingEffects(FString ActorId) = 0;
    virtual TFuture<bool> AcknowledgeEffect(FString ActorId, FGuid DeliveryId) = 0;
    virtual TFuture<bool> Backup(FString Destination) = 0;
    // 停止接收新任务，等待已入队事务结束，再关闭连接；析构必须调用。
    virtual void Close() = 0;
};
