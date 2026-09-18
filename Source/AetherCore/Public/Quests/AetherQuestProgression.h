#pragma once
#include "Profile/AetherProfileState.h"

struct AETHERCORE_API FAetherQuestProgressionDefinitions
{
    int32 SchemaVersion=1;
    TMap<FString,int32> QuestSkillPoints;
    bool Validate(const FAetherRules& Rules,FString& Reason) const;
    static FAetherQuestProgressionDefinitions Parse(const FString& Json,const FAetherRules& Rules,FString& Reason);
};
enum class EAetherQuestMutationCode:uint8 { Applied,Unchanged,Invalid,NotAllowed,Capacity };
struct FAetherQuestMutation
{
    EAetherQuestMutationCode Code=EAetherQuestMutationCode::Invalid;
    TArray<FString> ClaimedQuests;
    TArray<FGuid> DeferredRewards;
    int32 AwardedSkillPoints=0;
};
namespace AetherQuestProgression
{
    AETHERCORE_API bool Available(const FAetherProfileStateV10& Profile,const FString& QuestId,const FAetherRules& Rules);
    AETHERCORE_API bool Complete(const FAetherProfileStateV10& Profile,const FString& QuestId,const FAetherRules& Rules);
    // 仅接收场景/战斗处理器已验证的事实；客户端交互不能直接传任意 ObjectiveId 来调用。
    AETHERCORE_API bool Observe(FAetherProfileStateV10& Profile,const FString& ObjectiveId,const FAetherRules& Rules);
    // 自动任务与指定手动任务一起在候选副本结算。满包/金币上限时把整份物品和金币转为待领奖，
    // XP、完成标记和新任务点数仍在同一 Profile 候选内；调用者负责一次持久提交。
    AETHERCORE_API FAetherQuestMutation Settle(FAetherProfileStateV10& Profile,const TMap<FString,FString>& WorldFacts,
        const FString& ManualQuest,const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,
        const FAetherRules& Rules,const FAetherQuestProgressionDefinitions& Progression);
    // v10 新的任务成长点数领取途径；已有历史来源保留实际发放值，不按新配置补差或虚构旧付费退款。
    AETHERCORE_API bool HasClaimableSkillPoints(const FAetherProfileStateV10& Profile,const FAetherQuestProgressionDefinitions& Definitions);
    AETHERCORE_API FAetherQuestMutation ClaimSkillPoints(FAetherProfileStateV10& Profile,const FAetherV10ItemDefinitions& Items,
        const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,const FAetherQuestProgressionDefinitions& Progression);
}
