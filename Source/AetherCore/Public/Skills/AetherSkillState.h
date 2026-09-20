#pragma once
#include "CoreMinimal.h"
#include "Skills/AetherSkillDefinitions.h"

enum class EAetherSkillGrantSource:uint8 { Equipment, Temporary };
struct FAetherExternalSkillGrant
{
    FString SourceId,SkillId;
    int32 Rank=1;
    EAetherSkillGrantSource Source=EAetherSkillGrantSource::Equipment;
};
struct FAetherSkillPurchase
{
    FString SkillId;
    int32 Rank=1,PaidPoints=0;
    FGuid CommandId;
};
// 由权威用例构造，不能反序列化客户端的“安全/任务已完成”声明。
struct FAetherSkillRuleContext
{
    int32 CharacterLevel=1;
    TSet<FString> CompletedQuests;
    bool bAtResetService=false,bInCombat=false,bCasting=false,bCoolingDown=false;
};
enum class EAetherSkillMutationCode:uint8
{
    Applied,Unchanged,Invalid,Missing,NotReady,StoryRequired,Prerequisite,LevelRequired,
    QuestRequired,InsufficientPoints,MaxRank,NotAuthorized,Conflict,Capacity
};
struct FAetherSkillMutation
{
    EAetherSkillMutationCode Code=EAetherSkillMutationCode::Invalid;
    int32 PointsChanged=0; // 获得/退款为正，支付为负。
    TArray<FString> AffectedSkills;
};

// 仅持久化永久学习、故事授予、实付成本和点数来源；装备/临时来源每次由服务器重建。
// 不保存 SpecHandle、界面节点索引或 UObject。SkillId 失去临时授权时仍可留在快捷位中。
struct AETHERCORE_API FAetherSkillStateV10
{
    static constexpr int32 HotbarCapacity=4;
    static constexpr int32 MaxPointEvents=4096;
    static constexpr int32 MaxTotalPoints=1000000;
    TMap<FString,int32> LearnedRanks;
    TMap<FString,FString> StoryGrants; // SkillId -> 唯一故事来源事件；固定基础 rank 1。
    TMap<FString,int32> PointEvents; // 唯一来源事件 -> 实际发放点数，不回收、不按新配置重算。
    TArray<FAetherSkillPurchase> Purchases;
    TMap<int32,FString> Hotbar;
    int32 AvailableSkillPoints=0;

    int32 PermanentRank(const FString& SkillId) const;
    int32 EffectiveRank(const FString& SkillId,const TArray<FAetherExternalSkillGrant>& Grants) const;
    bool Validate(const FAetherSkillDefinitionsV10& Definitions,FString& Reason) const;
    static bool ValidateExternalGrants(const TArray<FAetherExternalSkillGrant>& Grants,const FAetherSkillDefinitionsV10& Definitions);

    // 都只发布经过完整校验的候选值。命令重放、版本与磁盘原子提交由外层协调者负责。
    FAetherSkillMutation AwardPoints(const FString& EventId,int32 Points,const FAetherSkillDefinitionsV10& Definitions);
    FAetherSkillMutation GrantStory(const FString& SkillId,const FString& EventId,const FAetherSkillDefinitionsV10& Definitions);
    // 详情与技能树使用同一学习条件查询；只读查询不创建支付记录或临时命令。
    EAetherSkillMutationCode CanLearnNext(const FString& SkillId,const FAetherSkillRuleContext& Context,const FAetherSkillDefinitionsV10& Definitions) const;
    FAetherSkillMutation LearnNext(const FString& SkillId,FGuid CommandId,const FAetherSkillRuleContext& Context,const FAetherSkillDefinitionsV10& Definitions);
    // 空 RootSkill 表示重置全部自由学习；非空表示该节点及失去前置的后继。
    FAetherSkillMutation Reset(const FString& RootSkill,const FAetherSkillRuleContext& Context,const FAetherSkillDefinitionsV10& Definitions,const TArray<FAetherExternalSkillGrant>& Grants={});
    FAetherSkillMutation Bind(int32 Slot,const FString& SkillId,const TArray<FAetherExternalSkillGrant>& Grants,const FAetherSkillDefinitionsV10& Definitions);
    static bool FromLegacyMask(uint8 Mask,const FAetherSkillDefinitionsV10& Definitions,FAetherSkillStateV10& Out,FString& Reason);
private:
    FAetherSkillMutation Publish(FAetherSkillStateV10&& Candidate,FAetherSkillMutation Result,const FAetherSkillDefinitionsV10& Definitions);
};
