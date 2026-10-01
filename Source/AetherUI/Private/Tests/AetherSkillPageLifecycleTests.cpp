#include "Misc/AutomationTest.h"
#include "Skills/AetherSkillTreePage.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Networking/AetherCommandClient.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSkillPageLifecycleTest,"Aether.V10.UI.SkillPageSnapshotLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSkillPageLifecycleTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Fixture world"),World))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* LocalPlayer=NewObject<ULocalPlayer>(GEngine);
    auto* Menu=NewObject<UAetherMenuSubsystem>(LocalPlayer);
    auto* Page=NewObject<UAetherSkillTreePage>(LocalPlayer);
    Page->Menu=Menu;
    Menu->OnChanged.AddUObject(Page,&UAetherSkillTreePage::HandleMenu);
    ON_SCOPE_EXIT
    {
        Menu->OnChanged.Clear();Menu->AttachPawn(nullptr);
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    };
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* First=World->SpawnActor<AAetherFrontierCharacter>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
    auto* Second=World->SpawnActor<AAetherFrontierCharacter>(FVector(500,0,0),FRotator::ZeroRotator,Params);
    if(!TestNotNull(TEXT("First pawn"),First)||!TestNotNull(TEXT("Replacement pawn"),Second))return false;
    Menu->AttachPawn(First);

    // 直接调用生产发布门；不创建第二套缓存实现，不伪造持久命令或 GAS 写者。
    FAetherInspectionSnapshot Snapshot;
    Snapshot.Context.OwnerIdentity=TEXT("LifecycleOwner");Snapshot.Context.SessionId=FGuid::NewGuid();
    Snapshot.ProfileRevision=7;Snapshot.bPresentationReady=true;Snapshot.bCanAct=true;
    const FString SameVersions=TEXT("same-profile-grants-and-cooldown-versions");
    Page->PublishNativeSnapshot(First,Snapshot,SameVersions);
    const auto FirstContext=Page->Snapshot.Context;
    TestTrue(TEXT("Initial source publishes a valid view"),FirstContext.IsValid()&&Page->NativePawn.Get()==First);
    const auto StableGeneration=Page->ViewGeneration;
    Page->PublishNativeSnapshot(First,Snapshot,SameVersions);
    TestEqual(TEXT("Unchanged valid source keeps its generation"),Page->ViewGeneration,StableGeneration);

    Snapshot.bCanAct=false;
    Page->PublishNativeSnapshot(First,Snapshot,SameVersions);
    TestFalse(TEXT("Action gate closure is visible without a profile revision"),Page->Snapshot.bCanAct);
    TestTrue(TEXT("Action gate closure invalidates previous intent context"),!Page->Snapshot.Context.Same(FirstContext));
    Snapshot.bCanAct=true;
    Page->PublishNativeSnapshot(First,Snapshot,SameVersions);
    TestTrue(TEXT("Action gate reopening is also visible at the same version"),Page->Snapshot.bCanAct);
    Snapshot.bPresentationReady=false;Snapshot.bCanAct=false;
    Page->PublishNativeSnapshot(First,Snapshot,SameVersions);
    TestFalse(TEXT("Synchronizing snapshots never retain the previous readiness"),Page->Snapshot.bPresentationReady);
    Snapshot.bPresentationReady=true;Snapshot.bCanAct=true;
    Page->PublishNativeSnapshot(First,Snapshot,SameVersions);

    const auto BeforeClose=Page->ViewGeneration;
    Menu->OpenPage(EAetherMenuPage::Skills);Menu->Close();Menu->OpenPage(EAetherMenuPage::Skills);
    Page->PublishNativeSnapshot(First,Snapshot,SameVersions);
    TestEqual(TEXT("Normal close and reopen do not churn a valid source"),Page->ViewGeneration,BeforeClose);
    Page->Selected=FAetherSkillNodeIdentity{TEXT("OldSelection"),1};
    Page->PendingNode=Page->Selected;Page->SourcePage=3;
    const auto OldPawnContext=Page->Snapshot.Context;
    Menu->AttachPawn(Second);
    TestFalse(TEXT("Real menu pawn replacement clears native view"),Page->bNativeSnapshot);
    TestTrue(TEXT("Pawn replacement invalidates cache and snapshot together"),Page->NativeSnapshotKey.IsEmpty()&&!Page->Snapshot.Context.IsValid());
    TestTrue(TEXT("Pawn replacement clears selection and pending presentation"),!Page->Selected.IsSet()&&!Page->PendingNode.IsSet()&&Page->SourcePage==0);
    Page->PublishNativeSnapshot(Second,Snapshot,SameVersions);
    TestTrue(TEXT("Same-version replacement repopulates instead of remaining empty"),Page->bNativeSnapshot&&Page->NativePawn.Get()==Second&&Page->Snapshot.Context.IsValid());
    TestFalse(TEXT("Replacement cannot inherit old UI intent identity"),Page->Snapshot.Context.Same(OldPawnContext));

    Page->Selected=FAetherSkillNodeIdentity{TEXT("OldSelection"),1};
    Snapshot.Context.OwnerIdentity=TEXT("lifecycleowner");
    const auto BeforeOwner=Page->ViewGeneration;
    Page->PublishNativeSnapshot(Second,Snapshot,SameVersions);
    TestTrue(TEXT("Owner identity remains case-sensitive despite identical version keys"),Page->ViewGeneration>BeforeOwner&&!Page->Selected.IsSet());
    Snapshot.Context.SessionId=FGuid::NewGuid();
    const auto BeforeChannel=Page->ViewGeneration;
    Page->PublishNativeSnapshot(Second,Snapshot,SameVersions);
    TestTrue(TEXT("New channel cannot reuse old generation"),Page->ViewGeneration>BeforeChannel&&Page->Snapshot.Context.SessionId==Snapshot.Context.SessionId);

    // 真实 HandleNativeProfile 的缺快照分支必须清除键；重发同版本快照仍能恢复。
    Page->CommandClient=NewObject<UAetherCommandClient>(LocalPlayer);
    const auto BeforeLoading=Page->ViewGeneration;
    Page->HandleNativeProfile();
    TestTrue(TEXT("Loading channel clears stale snapshot and version cache"),!Page->bNativeSnapshot&&Page->NativeSnapshotKey.IsEmpty()&&!Page->Snapshot.Context.IsValid());
    Page->CommandClient.Reset();
    Page->PublishNativeSnapshot(Second,Snapshot,SameVersions);
    TestTrue(TEXT("Same-version recovery after loading is published"),Page->bNativeSnapshot&&Page->ViewGeneration>BeforeLoading);

    // 模拟 Slate 销毁后同一 UObject 重建，生产 NativeDestruct 撤销订阅与旧展示。
    Page->NativeDestruct();
    TestFalse(TEXT("Destruction unsubscribes menu callbacks"),Menu->OnChanged.IsBoundToObject(Page));
    TestTrue(TEXT("Destruction invalidates the native cache"),!Page->bNativeSnapshot&&Page->NativeSnapshotKey.IsEmpty()&&!Page->NativePawn.IsValid());
    const auto BeforeReconstruct=Page->ViewGeneration;
    Page->PublishNativeSnapshot(Second,Snapshot,SameVersions);
    TestTrue(TEXT("Same UObject can republish unchanged version after reconstruction"),Page->bNativeSnapshot&&Page->ViewGeneration>BeforeReconstruct);
    return true;
}
#endif
