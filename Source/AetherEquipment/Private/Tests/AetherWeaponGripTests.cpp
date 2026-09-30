#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AetherEquipmentGrip.h"
#include "AetherEquipmentComponent.h"
#include "Engine/SkeletalMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherWeaponGripTransformTest,"Aether.Equipment.FullGripTransform",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherWeaponGripTransformTest::RunTest(const FString&)
{
    // Synthetic noncommuting rotations are test inputs, never production grip calibration.
    FAetherResolvedWeaponGrip G;G.bValid=true;
    G.SocketToMainBone=FTransform(FRotator(13,27,9),FVector(2,-3,4));
    G.WeaponToSocket=FTransform(FRotator(-19,36,17),FVector(5,7,-2),FVector(.6));
    G.SupportHandToWeapon=FTransform(FRotator(31,-22,8),FVector(0,40,3));
    const FTransform Bone(FRotator(7,-51,18),FVector(12,5,30),FVector(100));
    const FTransform Component(FRotator(-4,63,11),FVector(500,-70,90),FVector(2));
    FTransform Target;
    TestTrue(TEXT("Full target resolves"),AetherEquipmentGrip::SupportTarget(G,Bone,Component,Target));
    const FTransform BoneWorld=Bone*Component,SocketWorld=G.SocketToMainBone*BoneWorld;
    const FQuat WeaponRotation=SocketWorld.GetRotation()*G.WeaponToSocket.GetRotation();
    const FVector WeaponOrigin=SocketWorld.TransformPosition(G.WeaponToSocket.GetTranslation());
    const FVector ExpectedWorld=WeaponOrigin+WeaponRotation.RotateVector(G.SupportHandToWeapon.GetTranslation()*.6);
    const FQuat ExpectedRotation=Component.GetRotation().Inverse()*WeaponRotation*G.SupportHandToWeapon.GetRotation();
    TestTrue(TEXT("Socket offsets preserve parent scale; weapon geometry scale stays absolute"),
        Target.GetTranslation().Equals(Component.InverseTransformPosition(ExpectedWorld),1.e-4));
    TestTrue(TEXT("Target contains the full weapon-local support rotation"),
        FMath::Abs(Target.GetRotation()|ExpectedRotation)>1.-1.e-6);
    const FQuat Q=G.SupportHandToWeapon.GetRotation();
    G.SupportHandToWeapon.SetRotation(FQuat(-Q.X,-Q.Y,-Q.Z,-Q.W));FTransform Equivalent;
    TestTrue(TEXT("Quaternion sign is equivalent"),AetherEquipmentGrip::SupportTarget(G,Bone,Component,Equivalent)&&
        FMath::Abs(Target.GetRotation()|Equivalent.GetRotation())>1.-1.e-6);
    G.WeaponToSocket.SetScale3D(FVector(1,2,1));
    TestFalse(TEXT("Nonuniform weapon scale rejected"),AetherEquipmentGrip::SupportTarget(G,Bone,Component,Target));
    TestTrue(TEXT("Failure clears previous target"),Target.Equals(FTransform::Identity));
    G.WeaponToSocket.SetScale3D(FVector(1));G.SupportHandToWeapon.SetScale3D(FVector(2));
    TestFalse(TEXT("Support frame must be rigid"),AetherEquipmentGrip::SupportTarget(G,Bone,Component,Target));
    G.SupportHandToWeapon.SetScale3D(FVector(1));
    FTransform Invalid=Component;Invalid.SetScale3D(FVector(-1));
    TestFalse(TEXT("Reflected body scale rejected"),AetherEquipmentGrip::SupportTarget(G,Bone,Invalid,Target));
    Invalid.SetScale3D(FVector(0));
    TestFalse(TEXT("Singular body scale rejected"),AetherEquipmentGrip::SupportTarget(G,Bone,Invalid,Target));
    Invalid=Component;Invalid.SetRotation(FQuat(0,0,0,2));
    TestFalse(TEXT("Unnormalized rotation rejected"),AetherEquipmentGrip::SupportTarget(G,Bone,Invalid,Target));
    G.bValid=false;
    TestFalse(TEXT("Unconfigured or removed equipment has no target"),AetherEquipmentGrip::SupportTarget(G,Bone,Component,Target));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherWeaponGripDefinitionTest,"Aether.Equipment.FullGripDefinition",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherWeaponGripDefinitionTest::RunTest(const FString&)
{
    auto* D=NewObject<UAetherEquipmentDefinition>();
    TestFalse(TEXT("Default/old asset cannot invent a full grip"),D->HasValidSupportHandGrip());
    D->bSupportHandTransformConfigured=true;D->GripTargetMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Tests/Body.Body")));
    D->GripMainHandBone=TEXT("TestMain");D->GripSupportHandBone=TEXT("TestSupport");
    D->GripSourceSha256=FString::ChrN(64,'a');D->GripTransform=FTransform::Identity;
    TestTrue(TEXT("Explicit identity is valid when authored with provenance"),D->HasValidSupportHandGrip());
    D->GripSupportHandBone=D->GripMainHandBone;
    TestFalse(TEXT("A hand cannot support itself"),D->HasValidSupportHandGrip());
    D->GripSupportHandBone=TEXT("TestSupport");D->GripSourceSha256.Reset();
    TestFalse(TEXT("Source provenance required"),D->HasValidSupportHandGrip());
    return true;
}
#endif
