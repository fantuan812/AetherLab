#pragma once
#include "Inventory/AetherInventoryState.h"
#include "Inventory/AetherEconomyDefinitions.h"
#include "Skills/AetherSkillState.h"
#include "World/AetherContainerState.h"

// 只读展示身份由已授权拥有者快照提供；它不是客户端自报身份的认证凭证。
// SessionId 隔离重连/换 Pawn 的同号版本；SnapshotRevision 包括详情依赖的资源、状态和定义变更。
struct FAetherInspectContext
{
    FString OwnerIdentity;
    FGuid SessionId;
    int64 SnapshotRevision=-1;
    bool IsValid() const {return !OwnerIdentity.IsEmpty()&&OwnerIdentity.Len()<=128&&SessionId.IsValid()&&SnapshotRevision>=0&&SnapshotRevision<MAX_int64;}
    bool Same(const FAetherInspectContext& B) const
    {return OwnerIdentity.Equals(B.OwnerIdentity,ESearchCase::CaseSensitive)&&SessionId==B.SessionId&&SnapshotRevision==B.SnapshotRevision;}
};
enum class EAetherInspectTarget:uint8 {ItemInstance,ItemDefinition,EquipmentSlot,SkillNode,StatusEffect};
struct FAetherInspectTarget
{
    EAetherInspectTarget Kind=EAetherInspectTarget::ItemInstance;
    FGuid InstanceId; // EquipmentSlot 同时固定打开时的实例，不能只记住 Ring1 的字符串。
    FString DefinitionId,SlotId,ContainerId; // 容器来源是对象身份的一部分，不能按同一格索引替换。
    int32 SkillRank=0; // 0 表示整个技能；1..3 表示树中的指定等级节点。
    FString ComparisonSlot; // 空值不自动选 Ring1/Ring2；用户明确选择可替换槽。
};
struct FAetherInspectRequest {FAetherInspectContext Context;FAetherInspectTarget Target;};
struct FAetherInspectField {FString Key,Label,Value;};
struct FAetherInspectStatusEffect
{
    FGuid InstanceId;
    FString DefinitionId,DisplayName,IconId,Source;
    TArray<FAetherInspectField> Impacts;
    // 单调的服务器同步时间；无期限效果不写结束时间，界面也不制造倒计时。
    TOptional<double> ExpiresAtServerSeconds;
};
struct FAetherInspectionSnapshot
{
    FAetherInspectContext Context;
    // 展示版本不是数据库版本，命令必须使用独立的聚合版本。
    int64 ProfileRevision=-1,WorldRevision=-1;
    FAetherInventoryStateV10 Inventory;
    TOptional<FAetherContainerStateV10> Container;
    FGuid ContainerContext;
    int64 ContainerWorldRevision=-1;
    int32 Gold=0;
    bool bCanAct=false;
    FString TradeTargetStableId;
    TOptional<FAetherShopDefinitionV10> Shop;
    FAetherSkillStateV10 Skills;
    FAetherSkillRuleContext SkillContext;
    TArray<FAetherExternalSkillGrant> ExternalGrants;
    TArray<FAetherInspectStatusEffect> StatusEffects;
    double ServerTimeSeconds=0;
};
enum class EAetherInspectionState:uint8 {Ready,Changed,Missing,Invalid};
enum class EAetherInspectAction:uint8 {Equip,Unequip,Drop,Lock,Unlock,Favorite,Unfavorite,Learn,BindHotbar,TrackQuest,FocusSkill,Use,Split,Sell,Buy,Repair,Deposit,Withdraw,ResetSkills};
struct FAetherInspectionAction
{
    EAetherInspectAction Kind=EAetherInspectAction::Equip;
    FString Argument,Label,DisabledReason;
    int32 MaxQuantity=1;
    bool bEnabled=false,bNeedsConfirmation=false;
    FString ConfirmationSummary;
    int64 UnitPrice=0; // 仅展示；执行价格由服务器当前定义重新解析。
};
struct FAetherInspectionStatDifference
{
    FString Id;
    // 这里只显示定义允许的装备附加总值，不把尚未接入的基础伤害/词缀伪装成最终战斗属性。
    double Before=0,After=0;
    double Delta() const {return After-Before;}
};
struct FAetherInspectionModel
{
    FAetherInspectRequest Request;
    EAetherInspectionState State=EAetherInspectionState::Invalid;
    FString Title,IconId,Category,Message;
    TArray<FAetherInspectField> Fields;
    TArray<FAetherInspectionAction> Actions;
    TArray<FString> ComparisonSlots;
    TArray<FAetherInspectionStatDifference> Comparison;
    TArray<FGuid> DisplacedInstances;
    FString ComparisonMessage;
    TOptional<FAetherV10ItemInstance> Item;
    int32 PermanentSkillRank=0,EffectiveSkillRank=0;
    TOptional<FAetherSkillRankEffect> CurrentSkillEffect,NextSkillEffect,SelectedSkillEffect;
    TOptional<double> RemainingSeconds;
    bool CanInteract() const {return State==EAetherInspectionState::Ready;}
};

namespace AetherInspection
{
    // Pin 只固定对象，不提交任何动作。空装备槽也是可查看对象；以后出现物品时需重新打开。
    AETHERCORE_API FAetherInspectRequest Pin(const FAetherInspectionSnapshot& Snapshot,FAetherInspectTarget Target);
    AETHERCORE_API FAetherInspectionModel Build(const FAetherInspectRequest& Request,const FAetherInspectionSnapshot& Snapshot,
        const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills);
    AETHERCORE_API FString InventoryReason(EAetherInventoryMutationCode Code);
    AETHERCORE_API FString SkillReason(EAetherSkillMutationCode Code);
}
