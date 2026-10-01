#include "Misc/AutomationTest.h"
#include "Movement/AetherNavigationProbe.h"
#include "Combat/AetherCombat.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNavigationProbeTest,"Aether.AI.Navigation.LiveCapsuleAndWalkableGround",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNavigationProbeTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated collision world"),World))return false;
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);};
    const auto Box=[&](FVector Position,FVector Extent,FRotator Rotation=FRotator::ZeroRotator)
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Component=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Component);Actor->AddInstanceComponent(Component);Component->SetBoxExtent(Extent);
        Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Component->SetCollisionObjectType(ECC_WorldStatic);
        Component->SetCollisionResponseToAllChannels(ECR_Block);Component->RegisterComponent();
        Actor->SetActorLocationAndRotation(Position,Rotation);return Component;
    };
    auto* Ground=Box(FVector(0,0,-10),FVector(2000,2000,10));
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Character=World->SpawnActor<AAetherCharacter>(FVector(0,0,88),FRotator::ZeroRotator,Params);
    if(!TestNotNull(TEXT("Production character"),Character))return false;
    auto* Capsule=Character->GetCapsuleComponent();auto* Movement=Character->GetCharacterMovement();
    Movement->SetMovementMode(MOVE_Walking);
    FAetherNavigationProbeGeometry Geometry;FHitResult Floor;
    if(!TestTrue(TEXT("Read live registered movement geometry"),AetherNavigationProbe::ReadGeometry(*Character,Geometry)))return false;
    TestEqual(TEXT("Probe radius comes from the scaled capsule"),Geometry.Radius,Capsule->GetScaledCapsuleRadius());
    TestEqual(TEXT("Probe height comes from the scaled capsule"),Geometry.HalfHeight,Capsule->GetScaledCapsuleHalfHeight());
    TestTrue(TEXT("An exactly grounded capsule is not permanently blocked by ground contact"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    TestTrue(TEXT("Production CMC recognizes the returned support"),Movement->IsWalkable(Floor)&&Floor.GetComponent()==Ground);
    TestFalse(TEXT("Missing endpoint support is not a traversable local step"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(4000,0,0),Floor));
    const float InitialStepHeight=Movement->MaxStepHeight;Movement->MaxStepHeight=0;
    for(float Gap:{UCharacterMovementComponent::MIN_FLOOR_DIST,UCharacterMovementComponent::MAX_FLOOR_DIST})
    {
        Character->SetActorLocation(FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()+Gap));
        TestTrue(TEXT("Zero step height still admits flat ground at the normal CMC floor gap"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
        TestTrue(TEXT("Zero-step flat support is the actual ground"),Floor.GetComponent()==Ground&&Movement->IsWalkable(Floor));
    }
    Movement->MaxStepHeight=InitialStepHeight;Character->SetActorLocation(FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()));

    auto* Left=Box(FVector(230,-60,150),FVector(150,20,150));
    auto* Right=Box(FVector(230,60,150),FVector(150,20,150));
    TestTrue(TEXT("Current 68 cm body fits the 80 cm corridor"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Character->SetActorScale3D(FVector(1.5));Character->SetActorLocation(FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()));
    TestTrue(TEXT("Geometry is recaptured after live scale change"),AetherNavigationProbe::ReadGeometry(*Character,Geometry));
    TestEqual(TEXT("Live scale reaches the radius probe"),Geometry.Radius,Capsule->GetScaledCapsuleRadius());
    TestFalse(TEXT("Scaled body cannot borrow the old smaller corridor clearance"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Capsule->IgnoreActorWhenMoving(Left->GetOwner(),true);Capsule->IgnoreComponentWhenMoving(Right,true);
    TestTrue(TEXT("Actual movement actor/component ignore lists reach the probe"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Capsule->ClearMoveIgnoreActors();Capsule->ClearMoveIgnoreComponents();
    TestFalse(TEXT("Removing movement ignores restores blocking"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Capsule->SetCollisionObjectType(ECC_GameTraceChannel1);
    Left->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);Right->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    Left->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);Right->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);
    AetherNavigationProbe::ReadGeometry(*Character,Geometry);
    TestTrue(TEXT("Probe carries the live collision object channel"),Geometry.Channel==ECC_GameTraceChannel1);
    TestFalse(TEXT("Probe cannot substitute the ignored Pawn channel"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Left->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);Right->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);
    TestTrue(TEXT("Live response changes are honored"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Left->GetOwner()->Destroy();Right->GetOwner()->Destroy();Capsule->SetCollisionObjectType(ECC_Pawn);
    Character->SetActorScale3D(FVector::OneVector);Character->SetActorLocation(FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()));

    auto* Roof=Box(FVector(150,0,130),FVector(80,100,30));
    TestFalse(TEXT("Standing capsule cannot enter a low ceiling"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Movement->GetNavAgentPropertiesRef().bCanCrouch=true;Movement->SetCrouchedHalfHeight(44);Movement->Crouch(false);
    if(!TestTrue(TEXT("Production CMC really changes the live crouched capsule"),Capsule->GetUnscaledCapsuleHalfHeight()==44&&Character->IsCrouched()))return false;
    TestTrue(TEXT("Crouched body uses its current height under the same ceiling"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Movement->UnCrouch(false);
    TestTrue(TEXT("Production uncrouch restores the full body"),Capsule->GetUnscaledCapsuleHalfHeight()==88&&!Character->IsCrouched());
    TestFalse(TEXT("Uncrouching cannot reuse crouched clearance"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Roof->GetOwner()->Destroy();

    auto* Ramp=Box(FVector(0,1000,200),FVector(400,200,20),FRotator(10,0,0));
    Character->SetActorLocation(FVector(0,1000,330));Movement->SetWalkableFloorAngle(20);
    FFindFloorResult RampFloor;
    Movement->ComputeFloorDist(Character->GetActorLocation(),100,100,RampFloor,Capsule->GetScaledCapsuleRadius(),nullptr);
    if(!TestTrue(TEXT("Real ramp fixture is found by production CMC"),RampFloor.IsWalkableFloor()&&RampFloor.HitResult.GetComponent()==Ramp))return false;
    Character->SetActorLocation(Character->GetActorLocation()+Movement->GetGravityDirection()*(RampFloor.GetDistanceToFloor()-UCharacterMovementComponent::MIN_FLOOR_DIST));
    TestTrue(TEXT("CMC-accepted ramp remains locally usable"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    TestTrue(TEXT("Raised support search also resolves the downhill direction"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(-150,0,0),Floor));
    const float StepHeight=Movement->MaxStepHeight;Movement->MaxStepHeight=25;
    TestFalse(TEXT("Upward support change cannot exceed the current local step envelope"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    TestFalse(TEXT("Expanded downward search cannot permit a deeper drop than the step envelope"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(-150,0,0),Floor));
    Movement->MaxStepHeight=0;
    TestFalse(TEXT("Zero vertical envelope cannot borrow a default step height"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Movement->MaxStepHeight=StepHeight;
    Movement->SetWalkableFloorAngle(5);
    TestFalse(TEXT("Current CMC slope limit replaces the old fixed normal threshold"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Ramp->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Increase,20));
    TestTrue(TEXT("Per-component walkable slope override is still authoritative"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Ramp->GetOwner()->Destroy();Movement->SetWalkableFloorAngle(20);Character->SetActorLocation(FVector(0,0,88));

    Character->SteeringDirection=FVector::ForwardVector;Character->NextSteeringAt=Character->CombatTime()+1;
    Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestFalse(TEXT("Disabled live query component fails closed"),AetherNavigationProbe::ReadGeometry(*Character,Geometry));
    TestTrue(TEXT("Production steering cannot reuse a cached direction after collision is disabled"),Character->SafeMoveDirection(FVector(1000,0,88)).IsNearlyZero());
    Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Movement->SetUpdatedComponent(nullptr);
    TestFalse(TEXT("Missing movement-updated component has no probe fallback"),AetherNavigationProbe::IsLocalStepClear(*Character,FVector(150,0,0),Floor));
    Movement->SetUpdatedComponent(Capsule);Capsule->UnregisterComponent();
    TestFalse(TEXT("Unregistered capsule has no default geometry fallback"),AetherNavigationProbe::ReadGeometry(*Character,Geometry));
    return true;
}
#endif
