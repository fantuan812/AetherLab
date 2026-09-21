#include "Misc/AutomationTest.h"
#include "Movement/AetherDodgeAbility.h"
#include "Framework/AetherFrontier.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherGroundDodgeTest,"Aether.V10.Movement.GASGroundDodgeCostCollisionAndCancel",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherGroundDodgeTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated dodge world"),W))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT {W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false);};
    auto BoxAt=[&](FVector Position,FVector Extent)
    {
        auto* A=W->SpawnActor<AActor>();auto* B=NewObject<UBoxComponent>(A);
        A->SetRootComponent(B);A->AddInstanceComponent(B);B->SetBoxExtent(Extent);
        B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);B->SetCollisionResponseToAllChannels(ECR_Block);
        B->RegisterComponent();A->SetActorLocation(Position);return B;
    };
    BoxAt(FVector(0,0,-10),FVector(500,500,10));
    int32 Index=0;
    auto MakeCharacter=[&](FVector Position)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* C=W->SpawnActor<AAetherFrontierCharacter>(Position,FRotator::ZeroRotator,Params);
        auto* PS=W->SpawnActor<AAetherPlayerState>();PS->Profile.CharacterId=FString::Printf(TEXT("DodgeFixture%d"),++Index);
        C->SetPlayerState(PS);C->BindPersistentAbilities();C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());
        C->SetVitals(100,100,100);C->GrantSpells();
        C->GetCharacterMovement()->bRunPhysicsWithNoController=true;
        C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        return C;
    };
    auto* C=MakeCharacter(FVector(0,0,90));auto* M=C->GetCharacterMovement();
    auto* Blocked=MakeCharacter(FVector(0,250,90));auto* BM=Blocked->GetCharacterMovement();
    auto* Edge=MakeCharacter(FVector(430,-250,90));auto* EM=Edge->GetCharacterMovement();
    auto* Invalid=MakeCharacter(FVector(-250,-250,90));auto* IM=Invalid->GetCharacterMovement();
    ON_SCOPE_EXIT {for(auto* Player:{C,Blocked,Edge,Invalid})Player->CancelActions();};
    // 编辑器在游戏开始后的第一秒抑制边缘下落；夹具必须跨过该真实引擎启动保护。
    // 在授予动作成本前推进世界时间，不在测试动作中手工切换 Falling。
    W->SetBegunPlay(true);for(int I=0;I<12;++I)W->Tick(LEVELTICK_TimeOnly,.1f);
    TestTrue(TEXT("Fixture passed editor startup falling guard"),W->GetTimeSeconds()>=1.f);
    TestTrue(TEXT("Real granted GAS dodge activates"),C->TryDodge());
    TestEqual(TEXT("GameplayEffect charges once"),C->Stamina(),82.f);
    TestTrue(TEXT("Dodge action tag locks other actions"),C->AbilitySystem->HasMatchingGameplayTag(AetherDodge::ActiveTag()));
    TestFalse(TEXT("Duplicate activation rejected"),C->TryDodge());
    TestEqual(TEXT("Duplicate does not charge"),C->Stamina(),82.f);
    TestTrue(TEXT("RootMotionSource exists"),M->GetRootMotionSource(TEXT("Aether.Dodge")).IsValid());
    TestTrue(TEXT("Dodge begins on ground"),M->IsMovingOnGround());
    C->ReceiveHit(10,0,nullptr,false);TestEqual(TEXT("Server invulnerability window blocks combat hit"),C->Health(),100.f);
    bool Grounded=true;
    for(int I=0;I<8;++I){M->TickComponent(.05f,LEVELTICK_All,nullptr);Grounded&=M->IsMovingOnGround();}
    TestTrue(TEXT("Flat-ground dodge never forces falling"),Grounded);
    TestTrue(TEXT("Constant speed profile covers bounded real distance"),C->GetActorLocation().X>200&&C->GetActorLocation().X<270);
    TestTrue(TEXT("Floor contact retained"),FMath::Abs(C->GetActorLocation().Z-90)<5);
    C->CancelActions();M->TickComponent(.01f,LEVELTICK_All,nullptr);
    TestFalse(TEXT("Cancel removes dodge action tag"),C->AbilitySystem->HasMatchingGameplayTag(AetherDodge::ActiveTag()));
    TestFalse(TEXT("Cancel removes root motion source"),M->GetRootMotionSource(TEXT("Aether.Dodge")).IsValid());
    TestFalse(TEXT("Cancel removes remaining invulnerability"),C->AbilitySystem->HasMatchingGameplayTag(AetherDodge::InvulnerableTag()));
    TestEqual(TEXT("Cancel never refunds committed cost"),C->Stamina(),82.f);
    C->ReceiveHit(10,0,nullptr,false);TestEqual(TEXT("Combat hit resumes after cancel"),C->Health(),90.f);
    TestFalse(TEXT("Cancel does not bypass committed recovery/cooldown"),C->TryDodge());

    BoxAt(FVector(140,250,100),FVector(10,100,100));
    TestTrue(TEXT("Wall case activates"),Blocked->TryDodge());
    Grounded=true;for(int I=0;I<8;++I){BM->TickComponent(.05f,LEVELTICK_All,nullptr);Grounded&=BM->IsMovingOnGround();}
    TestTrue(TEXT("Wall sweep stops without penetration"),Blocked->GetActorLocation().X>40&&Blocked->GetActorLocation().X<100);
    TestTrue(TEXT("Wall does not induce upward launch"),Grounded);

    TestTrue(TEXT("Edge case begins on ground"),Edge->TryDodge());
    for(int I=0;I<8;++I)EM->TickComponent(.05f,LEVELTICK_All,nullptr);
    AddInfo(FString::Printf(TEXT("Edge location=%s velocity=%s mode=%d gravity=%.1f"),*Edge->GetActorLocation().ToString(),*EM->Velocity.ToString(),int32(EM->MovementMode),EM->GetGravityZ()));
    TestTrue(TEXT("Leaving platform naturally falls"),EM->IsFalling()&&Edge->GetActorLocation().Z<80);
    TestFalse(TEXT("Air cannot retrigger dodge"),Edge->TryDodge());

    Invalid->SetVitals(100,100,17);TestFalse(TEXT("Insufficient stamina rejects"),Invalid->TryDodge());
    TestEqual(TEXT("Rejection does not charge"),Invalid->Stamina(),17.f);
    Invalid->SetVitals(100,100,100);IM->SetMovementMode(MOVE_Falling);
    TestFalse(TEXT("Fresh air activation rejects"),Invalid->TryDodge());
    IM->SetMovementMode(MOVE_Walking);Invalid->SetCrouchInput(true);IM->UpdateCharacterStateBeforeMovement(.01f);
    TestFalse(TEXT("Crouched activation rejects"),Invalid->TryDodge());
    Invalid->SetCrouchInput(false);IM->UpdateCharacterStateBeforeMovement(.01f);Invalid->bTravelPending=true;
    TestFalse(TEXT("Travel rejects"),Invalid->TryDodge());
    return true;
}
#endif
