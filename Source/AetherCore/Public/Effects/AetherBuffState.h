#pragma once
#include "Attributes/AetherAttributeResolver.h"

enum class EAetherBuffReapply:uint8 { Refresh, Extend, AddStack, Independent, Reject };
enum class EAetherBuffOverflow:uint8 { Refresh, Reject, ReplaceSourceStack };
enum class EAetherBuffOperation:uint8 { Attribute, Skill, Tag, Heal, Damage };
struct FAetherBuffOperation
{
    EAetherBuffOperation Kind=EAetherBuffOperation::Attribute;
    FString Id;
    double Value=0;
    EAetherAttributeOperation AttributeOperation=EAetherAttributeOperation::Add;
};
struct FAetherBuffDefinition
{
    FString Id, DisplayName, IconId, Description, ExclusiveGroup;
    int32 Revision=1, Priority=0, MaxStacks=1;
    double Duration=10, Period=0;
    bool bPerSource=true, bResetPeriod=false, bDispellable=true;
    EAetherBuffReapply Reapply=EAetherBuffReapply::Refresh;
    EAetherBuffOverflow Overflow=EAetherBuffOverflow::Refresh;
    TArray<FString> Tags, DispelTags, ImmunityTags;
    TArray<FAetherBuffOperation> Operations;
    AETHERCORE_API bool Validate(FString& Reason) const;
};
struct AETHERCORE_API FAetherBuffDefinitions
{
    TMap<FString,FAetherBuffDefinition> Buffs;
    bool bValid=false;
    FString Error;
    static FAetherBuffDefinitions Parse(const FString& Json);
    static const FAetherBuffDefinitions& Get();
};
struct FAetherBuffInstance
{
    FGuid InstanceId, LifeId;
    FAetherBuffDefinition Definition;
    FString InstigatorId;
    TMap<FString,int32> Sources;
    double StartedAt=0, ExpiresAt=0, NextTickAt=0;
    uint64 TickIndex=0, Revision=1;
    bool bSuppressed=false;
    int32 Stacks() const {int32 N=0;for(const auto& P:Sources)N+=P.Value;return N;}
};
enum class EAetherBuffResult:uint8 { Applied, Refreshed, Replayed, Rejected, Immune, Capacity, Invalid };
struct FAetherBuffDueEvent
{
    FGuid InstanceId;
    uint64 TickIndex=0;
    double Time=0;
    bool bExpiry=false;
    int32 Stacks=1;
    TArray<FAetherBuffOperation> Operations;
};
class AETHERCORE_API FAetherBuffState
{
public:
    FGuid LifeId;
    uint64 Revision=0;
    TArray<FAetherBuffInstance> Instances;
    EAetherBuffResult Apply(const FAetherBuffDefinition& Definition,const FString& Source,double Now,FGuid DeliveryId={},const FString& Instigator={});
    bool RemoveSource(const FString& Source);
    bool Dispel(const FString& Tag);
    bool NextDue(double Now,FAetherBuffDueEvent& Event);
    void ResolveSuppression();
    bool HasTag(const FString& Tag) const;
    TArray<FAetherAttributeContribution> Attributes() const;
    TArray<FAetherExternalSkillGrant> SkillGrants() const;
private:
    TSet<FGuid> Deliveries;
};
