#include "Misc/AutomationTest.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "AetherFrontier.h"
#include "AetherGuide.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
AAetherFrontierProp* MakeTarget(UWorld* W,FName Id,FName Service,FVector Position)
{
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* P=W->SpawnActor<AAetherFrontierProp>(Position,FRotator::ZeroRotator,Params);
    P->Spec.Id=Id;P->Spec.bInteractiveMaterial=false;P->Service=Service;P->DispatchBeginPlay();
    // 只保留下面独立创建的遮挡体，避免占位立方体彼此挡住视线干扰目标评分断言。
    P->Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);return P;
}
AAetherFrontierCharacter* MakeViewer(UWorld* W)
{
    auto* C=W->SpawnActor<AAetherFrontierCharacter>();
    auto* PS=W->SpawnActor<AAetherPlayerState>();PS->Profile.CharacterId=TEXT("TargetViewer");
    C->SetPlayerState(PS);C->BindPersistentAbilities();
    C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());C->AbilitySystem->InitAbilityActorInfo(PS,C);
    C->SetVitals(100,100,100);
    W->GetSubsystem<UAetherNearbyRegistry>()->Register(C);return C;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNearbyRegistryTest,"Aether.V10.Interaction.LiveSpatialRegistrationAndUnload",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNearbyRegistryTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated world"),W))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT { W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false); };
    auto* Index=W->GetSubsystem<UAetherNearbyRegistry>();
    if(!TestNotNull(TEXT("World owns local spatial registry"),Index))return false;
    auto* P=MakeTarget(W,"Spatial.Test","Shop",FVector(-510,0,0));
    TestTrue(TEXT("Production BeginPlay registers target"),Index->Contains(P));
    TestTrue(TEXT("Negative-coordinate bucket query"),Index->Nearby(FVector(-520,0,0),20).Contains(P));
    Index->Register(P);
    TestEqual(TEXT("Duplicate registration has one spatial entry"),Index->Nearby(P->GetActorLocation(),20).Num(),1);
    P->SetActorLocation(FVector(1510,0,0));
    TestTrue(TEXT("Root transform update moves bucket immediately"),Index->Nearby(FVector(1510,0,0),20).Contains(P));
    TestTrue(TEXT("Old bucket cannot retain moved target"),Index->Nearby(FVector(-510,0,0),20).IsEmpty());
    TestTrue(TEXT("Unbounded query is refused"),Index->Nearby(FVector::ZeroVector,1001).IsEmpty());
    Index->Unregister(P);P->SetActorLocation(FVector(2000,0,0));
    TestFalse(TEXT("Unregister detaches transform callback"),Index->Contains(P));
    TestTrue(TEXT("Unregistered actor cannot be rediscovered by scan"),Index->Nearby(P->GetActorLocation(),20).IsEmpty());
    Index->Register(P);
    W->SetBegunPlay(true);P->Destroy();
    TestFalse(TEXT("Production EndPlay unregisters destroyed target"),Index->Contains(P));
    TestTrue(TEXT("No ghost candidate after unload"),Index->Nearby(FVector(2000,0,0),20).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherPinnedInteractionTest,"Aether.V10.Interaction.PinnedSelectionHysteresisAndLiveGuards",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherPinnedInteractionTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated interaction world"),W))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT { W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false); };
    auto* C=MakeViewer(W);auto* PS=C->ProfileState();
    auto* A=MakeTarget(W,"Target.A","Shop",FVector(200,5,0));
    auto* B=MakeTarget(W,"Target.B","Inn",FVector(200,-5,0));
    const auto Seen=AetherGuide::QueryTarget(C,A);
    TestTrue(TEXT("Specified target is executable"),AetherGuide::ValidateSelection(C,Seen));
    TestTrue(TEXT("Focus hysteresis keeps equally good previous target"),AetherGuide::SelectInteraction(C,B).Prop==B);
    B->SetActorLocation(FVector(100,0,0));A->SetActorLocation(FVector(-180,0,0));
    TestTrue(TEXT("Clearly better view direction changes focus"),AetherGuide::SelectInteraction(C,A).Prop==B);
    TestTrue(TEXT("Server validates original A even when B is now preferred"),AetherGuide::ValidateSelection(C,Seen));
    auto Forged=Seen;Forged.ActionId="Inn";
    TestFalse(TEXT("Neighbor action cannot be applied to original target"),AetherGuide::ValidateSelection(C,Forged));
    ++PS->Profile.Revision;
    TestFalse(TEXT("Progress change invalidates old displayed choice"),AetherGuide::ValidateSelection(C,Seen));--PS->Profile.Revision;
    A->Service="Register";
    TestFalse(TEXT("Changed target action invalidates cached choice"),AetherGuide::ValidateSelection(C,Seen));A->Service="Shop";
    A->Spec.Id="Changed.Id";
    TestFalse(TEXT("Changed durable identity invalidates cached choice"),AetherGuide::ValidateSelection(C,Seen));A->Spec.Id="Target.A";
    A->SetActorLocation(FVector(251,0,0));
    TestFalse(TEXT("Out of range does not authorize nearby replacement"),AetherGuide::ValidateSelection(C,Seen));
    A->SetActorLocation(FVector(200,5,0));
    C->bTravelPending=true;
    TestFalse(TEXT("Travel revokes pending interaction"),AetherGuide::ValidateSelection(C,Seen));C->bTravelPending=false;

    auto* Wall=W->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);Wall->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(10,100,100));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();
    Wall->SetActorLocation(FVector(100,0,0));
    TestFalse(TEXT("New occlusion revokes same target"),AetherGuide::ValidateSelection(C,Seen));Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Cleared occlusion restores eligibility"),AetherGuide::ValidateSelection(C,Seen));

    A->Service="TrainingExtinguished";A->SetOwner(B);
    TestTrue(TEXT("Other player's personal object has no offer"),AetherGuide::QueryTarget(C,A).Prompt.IsEmpty());
    A->SetOwner(C);
    const auto Physical=AetherGuide::QueryTarget(C,A);
    TestFalse(TEXT("Physical fire hint cannot execute E"),Physical.bExecutable);
    TestFalse(TEXT("Forged E for physical-only object is rejected"),AetherGuide::ValidateSelection(C,Physical));
    A->Service=NAME_None;
    TestTrue(TEXT("Background object has no prompt"),AetherGuide::QueryTarget(C,A).Prompt.IsEmpty());

    // 进入真实旧服务分发器，验证它不会在校验后重新选择分数更高的 B。
    A->Service="HingedGate";B->Service="HingedGate";
    auto* Mode=W->SpawnActor<AAetherFrontierMode>();const auto GateSelection=AetherGuide::QueryTarget(C,A);
    A->Mechanism->bGateOpen=false;B->Mechanism->bGateOpen=false;
    Mode->InteractTarget(C,GateSelection);
    TestTrue(TEXT("Real dispatcher toggles only originally selected gate"),A->Mechanism->bGateOpen&&!B->Mechanism->bGateOpen);
    ++PS->Profile.Revision;Mode->InteractTarget(C,GateSelection);
    TestTrue(TEXT("Stale request cannot toggle either gate"),A->Mechanism->bGateOpen&&!B->Mechanism->bGateOpen);--PS->Profile.Revision;
    A->bEnabled=false;
    TestTrue(TEXT("Disabled target offers no prompt"),AetherGuide::QueryTarget(C,A).Prompt.IsEmpty());A->bEnabled=true;
    A->Service="Shop";
    W->SetBegunPlay(true);A->Destroy();
    auto* Replacement=MakeTarget(W,"Target.A","Shop",FVector(200,5,0));
    TestTrue(TEXT("Replacement with same stable ID is independently registered"),W->GetSubsystem<UAetherNearbyRegistry>()->Contains(Replacement));
    TestFalse(TEXT("Same stable ID cannot revive old Actor selection"),AetherGuide::ValidateSelection(C,Seen));
    TestTrue(TEXT("New instance needs a new selection"),AetherGuide::ValidateSelection(C,AetherGuide::QueryTarget(C,Replacement)));
    return true;
}
#endif
