#pragma once
#include "Inspection/AetherInspectionService.h"

struct FAetherSkillNodeIdentity
{
    FString SkillId;
    int32 Rank=1;
    bool operator==(const FAetherSkillNodeIdentity& B) const
    {return Rank==B.Rank&&SkillId.Equals(B.SkillId,ESearchCase::CaseSensitive);}
};
struct FAetherSkillNodePlacement {FAetherSkillNodeIdentity Node;FVector2D Position=FVector2D::ZeroVector;};
struct AETHERCORE_API FAetherSkillTreeLayout
{
    TArray<FAetherSkillNodePlacement> Nodes;
    bool Validate(const FAetherSkillDefinitionsV10& Definitions,FString& Reason) const;
    static FAetherSkillTreeLayout Parse(const FString& Json,const FAetherSkillDefinitionsV10& Definitions,FString& Reason);
    static FAetherSkillTreeLayout Default(const FAetherSkillDefinitionsV10& Definitions);
    static const FAetherSkillTreeLayout& Get();
};
enum class EAetherSkillNodeState:uint8 {Unopened,Prerequisite,Insufficient,Available,Learned,Maximum,Granted,Pending,Blocked,Upgradeable};
struct FAetherSkillTreeNode
{
    FAetherSkillNodeIdentity Identity;
    FVector2D Position;
    FString Title,IconId,Reason;
    EAetherSkillMechanic Mechanic=EAetherSkillMechanic::Fire;
    EAetherSkillNodeState State=EAetherSkillNodeState::Unopened;
    bool bPermanent=false,bAuthorized=false;
};
struct FAetherSkillTreeEdge {int32 From=INDEX_NONE,To=INDEX_NONE;};
struct FAetherSkillHotbarView
{
    int32 Slot=0;
    FString SkillId,Title,IconId;
    int32 EffectiveRank=0;
    bool bAvailable=false;
};
struct FAetherSkillPointSourceView {FString EventId;int32 Points=0;};
struct FAetherSkillTreeModel
{
    FAetherInspectContext Context;
    bool bValid=false;
    FString Message;
    int32 AvailablePoints=0;
    TArray<FAetherSkillTreeNode> Nodes;
    TArray<FAetherSkillTreeEdge> Edges;
    TArray<FAetherSkillHotbarView> Hotbar;
    TArray<FAetherSkillPointSourceView> PointSources;
};
namespace AetherSkillTree
{
    AETHERCORE_API FAetherSkillTreeModel Build(const FAetherInspectionSnapshot& Snapshot,const FAetherSkillDefinitionsV10& Definitions,
        const FAetherSkillTreeLayout& Layout,const TOptional<FAetherSkillNodeIdentity>& Pending={});
    AETHERCORE_API FString StateLabel(EAetherSkillNodeState State);
    // 左右优先沿真实前置边导航，上下在空间中换路线；坐标只影响导航，不影响学习合法性。
    AETHERCORE_API int32 Navigate(const FAetherSkillTreeModel& Model,int32 Selected,FVector2D Direction);
}
