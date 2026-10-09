#include "AetherEquipmentGrip.h"

bool AetherEquipmentGrip::IsUniformTransform(const FTransform& Transform, bool RequireUnitScale)
{
    if (Transform.ContainsNaN() || FMath::Abs(Transform.GetRotation().SizeSquared() - 1.) > 1.e-4) return false;
    const FVector Scale = Transform.GetScale3D();
    if (Scale.GetMin() <= UE_SMALL_NUMBER || !Scale.Equals(FVector(Scale.X), 1.e-4)) return false;
    return !RequireUnitScale || Scale.Equals(FVector::OneVector, 1.e-4);
}

bool AetherEquipmentGrip::SupportTarget(const FAetherResolvedWeaponGrip& Grip,
    const FTransform& MainBoneComponent, const FTransform& ComponentWorld, FTransform& OutComponent)
{
    OutComponent = FTransform::Identity;
    if (!Grip.bValid || !IsUniformTransform(MainBoneComponent) || !IsUniformTransform(ComponentWorld) ||
        !IsUniformTransform(Grip.SocketToMainBone) || !IsUniformTransform(Grip.WeaponToSocket) ||
        !IsUniformTransform(Grip.SupportHandToWeapon, true)) return false;

    // Unreal transform multiplication applies left first. Socket translation keeps parent scale;
    // the attached weapon's geometry does not inherit socket/body scale (SetAbsolute(..., true)).
    const FTransform MainWorld = MainBoneComponent * ComponentWorld;
    const FTransform SocketWorld = Grip.SocketToMainBone * MainWorld;
    const FTransform WeaponWorld(
        SocketWorld.GetRotation() * Grip.WeaponToSocket.GetRotation(),
        SocketWorld.TransformPosition(Grip.WeaponToSocket.GetTranslation()),
        Grip.WeaponToSocket.GetScale3D());
    const FTransform HandWorld = Grip.SupportHandToWeapon * WeaponWorld;
    OutComponent = FTransform(
        ComponentWorld.GetRotation().Inverse() * HandWorld.GetRotation(),
        ComponentWorld.InverseTransformPosition(HandWorld.GetTranslation()), FVector::OneVector);
    return IsUniformTransform(OutComponent, true);
}
