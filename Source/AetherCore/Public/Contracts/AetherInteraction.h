#pragma once
#include "Contracts/AetherPlayerCommand.h"

enum class EAetherOfferAvailability : uint8 { Available, Locked, Busy, OutOfReach, Gone };
struct FAetherInteractionOffer
{
    FString TargetStableId, ActionId;
    int64 TargetRevision=-1;
    EAetherOfferAvailability Availability=EAetherOfferAvailability::Gone;
    FString ReasonId, DialogueId, QuestId, ServiceId;
    bool bPreferred=false;
};

// 查询必须只返回只读事实：不得授予技能、扣除物品、写存档或发送 RPC。
// 执行时携带 TargetStableId/ActionId/TargetRevision，由服务端重新检查所有条件，
// 不能把曾经显示为 Available 的 Offer 当成权限凭据。
struct FAetherInteractionQuery
{
    FString ServerActorId;
    FString TargetStableId;
};
class AETHERCORE_API IAetherInteractionProvider
{
public:
    virtual ~IAetherInteractionProvider()=default;
    virtual TArray<FAetherInteractionOffer> Query(const FAetherInteractionQuery& Context) const=0;
};
