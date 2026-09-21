#include "Misc/AutomationTest.h"
#include "Movement/AetherCharacterMovement.h"
#include "Framework/AetherFrontier.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherLocomotionTest,"Aether.V10.Movement.CapsuleHeadroomJumpAndSprintPrediction",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherLocomotionTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated movement world"),W))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT { W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false); };
    auto BoxAt=[&](FVector Position,FVector Extent)
    {
        auto* A=W->SpawnActor<AActor>();auto* B=NewObject<UBoxComponent>(A);
        A->SetRootComponent(B);A->AddInstanceComponent(B);B->SetBoxExtent(Extent);
        B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);B->SetCollisionObjectType(ECC_WorldStatic);
        B->SetCollisionResponseToAllChannels(ECR_Block);B->RegisterComponent();A->SetActorLocation(Position);return B;
    };
    BoxAt(FVector(0,0,-10),FVector(500,500,10));
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* C=W->SpawnActor<AAetherFrontierCharacter>(FVector(0,0,90),FRotator::ZeroRotator,Params);
    auto* PS=W->SpawnActor<AAetherPlayerState>();PS->Profile.CharacterId=TEXT("LocomotionFixture");
    C->SetPlayerState(PS);C->BindPersistentAbilities();C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());
    C->SetVitals(100,100,100);
    auto* M=Cast<UAetherCharacterMovement>(C->GetCharacterMovement());
    if(!TestNotNull(TEXT("Production character owns prediction-aware movement"),M))return false;
    M->SetMovementMode(MOVE_Walking);
    const float Standing=C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    TestEqual(TEXT("Default walk speed"),M->GetMaxSpeed(),450.f);
    C->SetSprintInput(true);M->UpdateCharacterStateBeforeMovement(.016f);
    TestTrue(TEXT("Ground sprint accepted"),C->bSprinting);
    TestEqual(TEXT("Single movement speed rule"),M->GetMaxSpeed(),625.f);
    C->Reactive->State.IceFraction=.8;
    TestEqual(TEXT("Sprint respects frozen movement penalty"),M->GetMaxSpeed(),312.5f);
    C->Reactive->State.IceFraction=0;
    C->SetCrouchInput(true);M->UpdateCharacterStateBeforeMovement(.016f);
    TestTrue(TEXT("Real capsule crouches"),C->IsCrouched());
    TestEqual(TEXT("Crouch capsule half-height"),C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),48.f);
    TestFalse(TEXT("Crouch cancels sprint intent"),M->bWantsSprint);
    TestEqual(TEXT("Crouch movement speed"),M->GetMaxSpeed(),190.f);

    // 低顶仅覆盖站姿胶囊，不与蹲姿相交；测试真实查询几何而非手工布尔标记。
    auto* Ceiling=BoxAt(FVector(0,0,150),FVector(150,150,20));
    C->StartJumpInput();
    TestTrue(TEXT("Low ceiling keeps real crouch"),C->IsCrouched());
    TestFalse(TEXT("Blocked stand never queues a jump"),C->bPressedJump);
    C->SetSprintInput(true);M->UpdateCharacterStateBeforeMovement(.016f);
    TestFalse(TEXT("Blocked stand cannot sprint"),C->bSprinting);
    TestEqual(TEXT("Blocked stand preserves capsule"),C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),48.f);
    Ceiling->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    C->StartJumpInput();
    TestFalse(TEXT("Jump first stands when clear"),C->IsCrouched());
    TestEqual(TEXT("Stand restores capsule"),C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),Standing);
    C->CheckJumpInput(.016f);
    TestTrue(TEXT("Single real CharacterMovement jump enters falling"),M->IsFalling());
    C->StopJumping();C->StartJumpInput();
    TestFalse(TEXT("Air cannot queue another jump"),C->bPressedJump);

    M->SetMovementMode(MOVE_Walking);C->ResetJumpState();C->SetCrouchInput(true);M->UpdateCharacterStateBeforeMovement(.016f);
    TestTrue(TEXT("Server replay fixture crouched"),C->IsCrouched());
    M->UpdateFromCompressedFlags(FSavedMove_Character::FLAG_JumpPressed);
    TestFalse(TEXT("Server compressed move resolves stand before checking jump"),C->IsCrouched());
    C->CheckJumpInput(.016f);TestTrue(TEXT("Server jump accepted after safe stand"),M->IsFalling());

    M->SetMovementMode(MOVE_Walking);C->ResetJumpState();C->StunUntil=C->CombatTime()+1;
    M->UpdateFromCompressedFlags(FSavedMove_Character::FLAG_Custom_0|FSavedMove_Character::FLAG_JumpPressed);
    TestFalse(TEXT("Forged held jump cannot bypass stun"),C->CanJump());
    TestFalse(TEXT("Forged sprint cannot bypass stun"),M->CanSprint());
    TestEqual(TEXT("Stun stops both standing and crouched movement"),M->GetMaxSpeed(),0.f);
    C->StunUntil=0;C->bTravelPending=true;TestFalse(TEXT("Travel rejects locomotion"),C->CanStartLocomotion());C->bTravelPending=false;
    C->SetVitals(100,100,0);TestFalse(TEXT("Exhaustion rejects jump"),C->CanJump());C->SetVitals(100,100,100);
    C->ReleaseHeldInput();C->bPanel=true;C->StartJumpInput();C->SetSprintInput(true);
    TestFalse(TEXT("Menu cannot start sprint"),M->bWantsSprint);
    TestFalse(TEXT("Menu cannot start jump"),C->bPressedJump);
    C->bPanel=false;C->bAttackHeld=true;M->bWantsSprint=true;C->Jump();
    C->ReleaseHeldInput();
    TestFalse(TEXT("Release clears held jump"),C->bPressedJump);
    TestFalse(TEXT("Release clears sprint intent"),M->bWantsSprint);
    TestFalse(TEXT("Canceled attack cannot fire on later release"),C->bAttackHeld);

    FSavedMovePtr Off(new FAetherSavedMove()),On(new FAetherSavedMove());
    auto* SprintMove=static_cast<FAetherSavedMove*>(On.Get());SprintMove->bSavedWantsSprint=true;
    TestTrue(TEXT("Sprint intent encoded in movement packet"),(SprintMove->GetCompressedFlags()&FSavedMove_Character::FLAG_Custom_0)!=0);
    TestFalse(TEXT("Press/release edges never combine"),SprintMove->CanCombineWith(Off,C,.125f));
    SprintMove->PrepMoveFor(C);TestTrue(TEXT("Correction replay restores original input"),M->bWantsSprint);
    SprintMove->Clear();TestFalse(TEXT("Recycled saved move has no stale sprint"),SprintMove->bSavedWantsSprint);
    return true;
}
#endif
