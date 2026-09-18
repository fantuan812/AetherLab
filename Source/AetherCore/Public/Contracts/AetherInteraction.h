#pragma once
#include "Contracts/AetherPlayerCommand.h"

enum class EAetherOfferAvailability:uint8 { Hidden, TalkOnly, Available, DisabledWithReason };
struct FAetherInteractionOffer
{
    FString TargetStableId,ActionId,DisplayVerb,IconId;
    int64 ProfileRevision=-1,WorldRevision=-1,TargetRevision=-1;
    int32 Priority=0;
    EAetherOfferAvailability Availability=EAetherOfferAvailability::Hidden;
    FString ReasonId,DialogueId,QuestId,ObjectiveId,ServiceId;
    TMap<FString,FString> ReasonParameters;
    bool bPreferred=false;
};
struct FAetherInteractionQuery
{
    FString ServerActorId;
    FString TargetStableId;
};
// UI 保存所见动作的完整身份，不用数组下标或“当前最近对象”替代。
// 请求 v2 携带独立 InteractionRevision；v1 仅保留读取，不授权新交互执行。
struct FAetherInteractionSelection
{
    FString TargetStableId,ActionId;
    int64 ProfileRevision=-1,WorldRevision=-1,InteractionRevision=-1;
};
// 查询不得授予技能、扣物品、生成训练对象、写存档或发 RPC；Offer 不是权限凭据。
class AETHERCORE_API IAetherInteractionProvider
{
public:
    virtual ~IAetherInteractionProvider()=default;
    virtual TArray<FAetherInteractionOffer> Query(const FAetherInteractionQuery& Context) const=0;
};
