#pragma once
#include "Contracts/AetherTransactionalStore.h"
#include "Profile/AetherProfileState.h"
#include "World/AetherWorldState.h"

enum class EAetherServerFactKind:uint8 {Personal,World,Settle,Daily,EncounterReward,LegacyLoot,EquipmentWear};
struct FAetherServerFact
{
    FString CharacterId,FactId,SourceId,UtcDay;
    TArray<FGuid> WornItems; // 命中时固定的实例，批处理不能改成提交时当前装备。
    int32 WearCount=1; // 仅合并尚未开始事务的连续同实例事件；保留全部磨损计数。
    FGuid InstanceId; // 服务器遭遇/旧掉落实例，不接受客户端任意奖励 ID。
    EAetherServerFactKind Kind=EAetherServerFactKind::Personal;
};
struct FAetherServerFactCompletion
{
    FAetherServerFact Event;
    EAetherStoreCode Code=EAetherStoreCode::Unavailable;
    TOptional<FAetherProfileStateV10> Profile;
    TOptional<FAetherWorldStateV10> World;
    FString Detail;
};
// 仅服务器可信战斗/场景事实可进入；这不是客户端任意 ObjectiveId 写入口。
// 事实/任务 Claims/点数来源均持久幂等，重复观察不会重复发奖。
class AETHERGAMEPLAY_API FAetherServerFactCoordinator
{
public:
    explicit FAetherServerFactCoordinator(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store);
    ~FAetherServerFactCoordinator();
    bool Enqueue(FAetherServerFact Event,FString& Reason);
    TArray<FAetherServerFactCompletion> Poll(double ServerMonotonicSeconds);
    int32 PendingCount() const;
    bool HasPendingForCharacter(const FString& CharacterId) const;
    bool HasPendingFact(const FString& CharacterId,const FString& FactId) const;
private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
