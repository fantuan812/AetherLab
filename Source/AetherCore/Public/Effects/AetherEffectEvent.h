#pragma once
#include "CoreMinimal.h"

enum class EAetherEffectEventKind : uint8
{
    LegacyAction, ResourceAdvanceInterval, Damage, BuffApply, BuffRefresh,
    BuffTick, BuffExpire, BuffDispel, ProjectionRefresh, ConsumableDelivery, LifeEnded
};

// Game-thread sequence is the ordering authority. IDs are values, never object addresses.
struct FAetherEffectEventIdentity
{
    EAetherEffectEventKind Kind=EAetherEffectEventKind::LegacyAction;
    uint64 Sequence=0;
    double LogicalTime=0;
    FGuid LifeId, ExecutionId, DeliveryId;
    int64 StateRevision=0;
};

// Only adjacent intervals with identical rates/context may merge. A discrete event is a boundary.
struct FAetherResourceAdvanceInterval
{
    FAetherEffectEventIdentity Identity;
    double Start=0, End=0, ManaRate=5, StaminaRate=4, PostureRate=0;
    double HazardRate=0,Defense=0;
    uint32 SourceIdentity=0;
    bool bPreserveSteps=false;
    bool CanMerge(const FAetherResourceAdvanceInterval& Other) const
    {
        return Identity.LifeId==Other.Identity.LifeId && Identity.StateRevision==Other.Identity.StateRevision &&
            End==Other.Start && ManaRate==Other.ManaRate && StaminaRate==Other.StaminaRate &&
            PostureRate==Other.PostureRate && HazardRate==Other.HazardRate && Defense==Other.Defense &&
            SourceIdentity==Other.SourceIdentity && bPreserveSteps==Other.bPreserveSteps && Other.End-Start<=30.;
    }
};
