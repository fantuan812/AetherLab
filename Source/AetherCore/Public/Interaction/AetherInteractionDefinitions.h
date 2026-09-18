#pragma once
#include "Contracts/AetherInteraction.h"
#include "Definitions/AetherRules.h"
#include "Inventory/AetherEconomyDefinitions.h"

enum class EAetherInteractionActionKind:uint8
{
    Talk,TrackObjective,Register,BindInn,Rest,LearnStorySkills,Train,ResetSkills,Trade,Repair,ClaimQuest,ClaimSkillPoints
};
struct FAetherInteractionActionDefinition
{
    FString Id,Verb,IconId,DialogueId,QuestId,ObjectiveId,ServiceId;
    EAetherInteractionActionKind Kind=EAetherInteractionActionKind::Talk;
    int32 Priority=0;
    TArray<FString> RequiredClaims,RequiredEvidence,HideAfterClaims,HideAfterEvidence;
    bool bSafeOnly=false,bHideLocked=false;
};
struct FAetherDialogueOption
{
    FString Label,ActionId,NextNodeId;
};
struct FAetherDialogueNode
{
    FString Id,Speaker,Text;
    TArray<FAetherDialogueOption> Options;
};
struct FAetherDialogueChoiceView
{
    FString Label,ActionId,NextNodeId,ReasonId;
    TMap<FString,FString> ReasonParameters;
    EAetherOfferAvailability Availability=EAetherOfferAvailability::Available;
};
struct FAetherDialogueView
{
    FString NodeId,Speaker,Text;
    TArray<FAetherDialogueChoiceView> Choices;
};
struct FAetherInteractionDefinition
{
    FString Id;
    TArray<FAetherInteractionActionDefinition> Actions;
    TMap<FString,FAetherDialogueNode> Dialogue;
};
struct AETHERCORE_API FAetherInteractionDefinitions
{
    int32 SchemaVersion=1;
    TMap<FString,FAetherInteractionDefinition> Targets;
    bool Validate(const FAetherRules& Rules,const FAetherEconomyDefinitionsV10& Economy,FString& Reason) const;
    static FAetherInteractionDefinitions Parse(const FString& Json,const FAetherRules& Rules,const FAetherEconomyDefinitionsV10& Economy,FString& Reason);
};

// 仅公开条件快照：场景适配器负责已加载/范围/视线/活动状态；个人条件来自对应角色已提交状态。
// 私有目标必须先过滤 OwnerCharacterId，不能把他人的任务进度放入 ReasonParameters。
struct FAetherInteractionSnapshot
{
    FString CharacterId,TargetStableId,DefinitionId,OwnerCharacterId;
    int64 ProfileRevision=-1,WorldRevision=-1,InteractionRevision=-1;
    TSet<FString> Claims,Evidence;
    bool bLoaded=false,bInRange=false,bLineOfSight=false,bActorCanAct=false;
    bool bBusy=false,bThreatened=false,bDowned=false,bInCombat=false;
    bool bHasClaimableSkillPoints=false;
    bool bHasStoryGrantAvailable=false; // 从本角色永久授予账本与故事进度计算，不是客户端许可。
    TSet<EAetherInteractionActionKind> RegisteredHandlers;
};
struct FAetherObjectiveGuidance
{
    FString QuestId,ObjectiveId,Label,Hint,AnchorId;
    FVector PersistentPosition=FVector::ZeroVector;
    bool bHasTarget=false,bRewardReady=false;
};
namespace AetherInteractionQueries
{
    AETHERCORE_API FAetherObjectiveGuidance Guidance(const FAetherInteractionSnapshot& Snapshot,const FAetherRules& Rules,const FString& PreferredQuest={});
}
// 构造后快照不变；客户端展示和服务器复验使用同一值规则，服务器每次执行前重新构造快照。
class AETHERCORE_API FAetherInteractionProvider final:public IAetherInteractionProvider
{
public:
    FAetherInteractionProvider(FAetherInteractionDefinition Definition,FAetherInteractionSnapshot Snapshot,FAetherRules Rules);
    virtual TArray<FAetherInteractionOffer> Query(const FAetherInteractionQuery& Context) const override;
    // 只能进入当前可交谈入口及其可达节点；动作选项由同一 Offer 过滤，隐藏条件不从对话旁路泄露。
    TOptional<FAetherDialogueView> QueryDialogue(const FAetherInteractionQuery& Context,const FString& NodeId) const;
    EAetherCommandCode CheckSelection(const FAetherInteractionQuery& Context,const FAetherInteractionSelection& Selection) const;
    // v1 虽可解码用于回执兼容，但缺少目标版本，不得授权新交互执行。
    EAetherCommandCode CheckCommand(const FString& ServerCharacterId,const FAetherPlayerCommand& Command) const;
    FAetherObjectiveGuidance Guidance() const;
private:
    const FAetherInteractionDefinition Definition;
    const FAetherInteractionSnapshot Snapshot;
    const FAetherRules Rules;
};
