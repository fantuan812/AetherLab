#pragma once
#include "Contracts/AetherTransaction.h"

// 资源版本属于一次 Pawn 生命；不能用角色 ID 或 Profile 版本替代。所有值均由服务器采样。
struct AETHERCORE_API FAetherResourceStateV10
{
    FGuid LifeId;
    int64 Revision=0, UseReadyAtUnixMs=0;
    double Health=100, Mana=100, Stamina=100;
    double MaxHealth=100, MaxMana=100, MaxStamina=100;
    bool Validate() const;
    bool Same(const FAetherResourceStateV10& Other) const;
};
struct FAetherConsumableEffectV10
{
    FGuid DeliveryId, ItemInstanceId;
    FString DefinitionId;
    int64 ProfileRevision=0;
    FAetherResourceStateV10 Before, After;
};
namespace AetherConsumableEffects
{
    // ACEF/1，显式小端编码；记录目标状态，恢复时不重新读取可能已变更的道具平衡表。
    AETHERCORE_API bool Encode(const FAetherConsumableEffectV10& Effect,TArray<uint8>& Bytes);
    AETHERCORE_API bool Decode(const TArray<uint8>& Bytes,FAetherConsumableEffectV10& Effect);
}
enum class EAetherEffectApplyCode : uint8 { Applied, Replayed, Conflict, Invalid, Capacity };

// 游戏线程资源所有者的提交屏障。生产适配器必须让资源修改通过它，并在 Busy 时延后整个
// 动作（包括死亡/花费/伤害副作用），不能丢弃修改。尚未连接旧 ASC 的路径不可声称受到保护。
class AETHERCORE_API FAetherConsumableReceiver
{
public:
    FAetherConsumableReceiver(FString ServerCharacterId,FAetherResourceStateV10 Initial);
    const FString& CharacterId() const { return Owner; }
    FAetherConsumableReceiver(const FAetherConsumableReceiver&)=delete;
    FAetherConsumableReceiver& operator=(const FAetherConsumableReceiver&)=delete;
    FGuid InstanceId() const { return ReceiverId; }
    const FAetherResourceStateV10& State() const { return Current; }
    bool Reserve(FGuid CommandId,FAetherResourceStateV10& Before);
    bool IsReserved(FGuid CommandId) const { return Reserved==CommandId&&CommandId.IsValid(); }
    // 只接受已知未提交/已回滚结果；超时或丢 ACK 不能据此解锁，必须先查询持久回执。
    bool CancelUncommitted(FGuid CommandId);
    bool UpdateResources(const FAetherResourceStateV10& Next);
    EAetherEffectApplyCode Apply(const FAetherEffectDelivery& Delivery,const FString& ServerCharacterId);
    // 数据库确认删除后才可释放去重槽；重复的已读旧投递仍会因资源版本不符而被拒绝。
    bool ForgetAcknowledged(FGuid DeliveryId);
    // 发起完整 pending 查询之前取此集合；查询中已不存在的旧 ID 可安全回收去重槽。
    TArray<FGuid> PendingAcknowledgementIds() const;
    // 明确的“新生命全资源重生”恢复策略，只能在玩家输入/战斗开启前调用。
    // 调用方先等旧写者完成并读取全部 pending；此函数不会把旧生命治疗加到新生命上。
    bool RecoverAtFullRespawn(const TArray<FAetherEffectDelivery>& Pending,const FString& ServerCharacterId);
private:
    const FString Owner;
    const FGuid ReceiverId=FGuid::NewGuid();
    FAetherResourceStateV10 Current;
    FGuid Reserved;
    TMap<FGuid,TArray<uint8>> Applied;
};
