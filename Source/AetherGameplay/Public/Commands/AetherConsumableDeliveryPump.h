#pragma once
#include "Commands/AetherProfileCoordinator.h"

// 每个 Poll 重新从当前会话/Pawn 解析资源所有者；指针仅在本次游戏线程调用中使用，不进入后台任务。
// 返回 nullptr 表示当前代次已失效。解析器不能重入调度器。
using FAetherResolveConsumableReceiver=TFunction<FAetherConsumableReceiver*(const FAetherProfileSession&)>;
// 将 receiver 当前资源发布到当前 ASC，必须在 ACK 删除持久投递之前成功。
// 不能重新播放旧 After；回放时 receiver 可能已经含有随后发生的伤害。
using FAetherPublishConsumableResources=TFunction<bool(const FAetherProfileSession&,const FAetherConsumableReceiver&)>;
enum class EAetherDeliveryPumpCode:uint8 { Idle, Pending, Complete, StaleSession, Invalid, Conflict, StorageUnavailable };
struct FAetherDeliveryPumpResult
{
    EAetherDeliveryPumpCode Code=EAetherDeliveryPumpCode::Idle;
    int32 Acknowledged=0;
    // 若恢复已发生而 ACK 失败，下一批使用普通投递模式，不再次初始化新生命。
    bool bFullRespawnRecovered=false;
    FGuid BlockedDeliveryId;
};

// 只轮询已就绪 Future；不会在游戏线程等待 SQLite。它调度一个有界读取批次，
// Complete 不代表随后不会有新事务：登录恢复时必须先排空旧写者，并维持输入/新写入屏障。
class AETHERGAMEPLAY_API FAetherConsumableDeliveryPump
{
public:
    explicit FAetherConsumableDeliveryPump(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store);
    ~FAetherConsumableDeliveryPump();
    bool Start(const FAetherProfileSession& Session,bool RecoverFullRespawn=false);
    FAetherDeliveryPumpResult Poll(const FAetherResolveConsumableReceiver& Resolve,const FAetherPublishConsumableResources& Publish={});
    bool IsPending() const;
private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
