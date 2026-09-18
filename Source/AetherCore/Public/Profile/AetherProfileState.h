#pragma once
#include "Inventory/AetherInventoryState.h"
#include "Skills/AetherSkillState.h"
#include "Definitions/AetherRules.h"

struct FAetherPendingRewardV10
{
    FGuid RewardId;
    FString SourceId;
    int32 Gold=0;
    TMap<FString,int32> Items;
};
// 冻结旧成功回执的事实用于迁移审计/拒绝旧协议，不把旧 Action 字符串转换为可执行的新命令。
struct FAetherLegacyInventoryReceiptV9
{
    FGuid CommandId,ItemInstanceId,DestinationInstanceId;
    int32 ExpectedRevision=0,FinalRevision=0,Quantity=0,Transferred=0;
    FString Action,DefinitionId,ShopId;
};
struct AETHERCORE_API FAetherProfileStateV10
{
    FString CharacterId;
    int64 Revision=0;
    int32 Gold=0,Experience=0;
    FAetherInventoryStateV10 Inventory;
    FAetherSkillStateV10 Skills;
    bool bRegistered=false,bCompanion=false;
    FGuid LastAbbeyReceipt,LastRelayReceipt;
    TArray<FString> Evidence,Claims,DailyEvidence,DailyClaims;
    FString DailyDate;
    TArray<FAetherPendingRewardV10> PendingRewards;
    TSet<FGuid> ClaimedRewardIds;
    // 导入来源留在 DTO 内，提交版本不归零。真正防止重复整档导入还需要数据库迁移标识。
    int32 LegacySaveSchema=0,LegacyProfileRevision=0;
    FString LegacySourceSha256;
    TArray<FAetherLegacyInventoryReceiptV9> LegacyInventoryReceipts;

    bool Validate(const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& SkillDefs,const FAetherRules& Rules,FString& Reason) const;
};
