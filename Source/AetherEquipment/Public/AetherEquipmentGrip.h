#pragma once
#include "CoreMinimal.h"

// Game-thread snapshot of validated asset data. No component/socket UObject is read by the anim worker.
struct FAetherResolvedWeaponGrip
{
    bool bValid = false;
    FName MainBone, SupportBone;
    FTransform SocketToMainBone = FTransform::Identity;
    FTransform WeaponToSocket = FTransform::Identity;
    FTransform SupportHandToWeapon = FTransform::Identity;
};

namespace AetherEquipmentGrip
{
    // Negative, zero and nonuniform scales are unsupported: silently discarding them changes handedness/shear.
    AETHEREQUIPMENT_API bool IsUniformTransform(const FTransform& Transform, bool RequireUnitScale = false);
    // Matches the visual's absolute world scale. MainBoneComponent is from this evaluation, after main-hand IK.
    AETHEREQUIPMENT_API bool SupportTarget(const FAetherResolvedWeaponGrip& Grip,
        const FTransform& MainBoneComponent, const FTransform& ComponentWorld, FTransform& OutComponent);
}
