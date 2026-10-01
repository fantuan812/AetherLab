#include "Misc/AutomationTest.h"
#include "Startup/AetherStartupOverlay.h"
#include "Startup/AetherStartupPresentationSubsystem.h"
#include "Startup/AetherStartupClient.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherStartupPresentationTest,"Aether.V10.UI.StartupPresentationLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherStartupPresentationTest::RunTest(const FString&)
{
    auto* Button=NewObject<UAetherStartupActionButton>();
    const auto First=FGuid::NewGuid(),Second=FGuid::NewGuid();
    int32 Requests=0;FGuid Requested;EAetherStartupIntent Intent=EAetherStartupIntent::Start;
    Button->OnRequested.AddLambda([&](EAetherStartupIntent Action,FGuid Token){++Requests;Requested=Token;Intent=Action;});
    Button->Present(EAetherStartupIntent::Start,First,EAetherStartupStage::Frontend,true);
    Button->OnClicked.Broadcast();TestEqual(TEXT("Click without press cannot invent intent"),Requests,0);
    Button->OnPressed.Broadcast();
    Button->Present(EAetherStartupIntent::Start,First,EAetherStartupStage::Frontend,true);
    Button->OnClicked.Broadcast();
    TestTrue(TEXT("Unchanged view preserves a real held click"),Requests==1&&Requested==First&&Intent==EAetherStartupIntent::Start);
    Button->OnClicked.Broadcast();TestEqual(TEXT("Consumed press cannot dispatch twice"),Requests,1);

    Button->OnPressed.Broadcast();
    Button->Present(EAetherStartupIntent::Start,Second,EAetherStartupStage::Frontend,true);
    Button->OnClicked.Broadcast();TestEqual(TEXT("Old release cannot start the next local attempt"),Requests,1);
    Button->Present(EAetherStartupIntent::Cancel,Second,EAetherStartupStage::WaitingForBackend,true);
    Button->OnPressed.Broadcast();
    Button->Present(EAetherStartupIntent::Cancel,Second,EAetherStartupStage::ReadingStorage,true);
    Button->OnClicked.Broadcast();TestEqual(TEXT("Phase transition revokes the held press even with the same token"),Requests,1);
    Button->OnPressed.Broadcast();
    Button->Present(EAetherStartupIntent::Cancel,Second,EAetherStartupStage::ReadingStorage,false);
    Button->Present(EAetherStartupIntent::Cancel,Second,EAetherStartupStage::ReadingStorage,true);
    Button->OnClicked.Broadcast();TestEqual(TEXT("Disable and enable cannot resurrect a stale press"),Requests,1);
    Button->OnPressed.Broadcast();
    Button->Present(EAetherStartupIntent::Retry,Second,EAetherStartupStage::ReadingStorage,true);
    Button->OnClicked.Broadcast();TestEqual(TEXT("Action replacement cannot reinterpret a held click"),Requests,1);
    Button->OnPressed.Broadcast();Button->ReleaseSlateResources(false);Button->OnClicked.Broadcast();
    TestEqual(TEXT("Slate resource release invalidates press state"),Requests,1);
    Button->OnPressed.Broadcast();Button->OnClicked.Broadcast();
    TestTrue(TEXT("Fresh intent after cancellation retains exact current token"),Requests==2&&Requested==Second&&Intent==EAetherStartupIntent::Retry);

    auto* GI=NewObject<UGameInstance>(GEngine);
    auto* Player=NewObject<ULocalPlayer>(GEngine);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World without controller or game state"),World))return false;
    ON_SCOPE_EXIT {Button->OnRequested.Clear();World->DestroyWorld(false);};
    auto* Overlay=NewObject<UAetherStartupOverlay>(GI);
    Overlay->SetPlayerContext(FLocalPlayerContext(Player,World));
    TestTrue(TEXT("Pre-HUD widget binds explicit local-player/world context"),Overlay->GetOwningLocalPlayer()==Player&&Overlay->GetWorld()==World);
    TestNull(TEXT("No player controller is required for the context"),Overlay->GetOwningPlayer());
    TestNull(TEXT("No game state is required for the context"),World->GetGameState());
    const auto Config=Overlay->GetDesiredInputConfig();
    TestTrue(TEXT("CommonUI owns the blocking menu configuration"),Config.IsSet()&&Config->bIgnoreMoveInput&&Config->bIgnoreLookInput);
    Overlay->CancelButton=Button;
    Button->Present(EAetherStartupIntent::Cancel,Second,EAetherStartupStage::WaitingForBackend,true);
    Button->OnPressed.Broadcast();Overlay->ClearPresentation();Button->OnClicked.Broadcast();
    TestEqual(TEXT("Overlay removal invalidates outstanding UI intent"),Requests,2);
    TestFalse(TEXT("Removed overlay has no actionable button"),Button->GetIsEnabled());

    for(const auto Stage:{EAetherStartupStage::Frontend,EAetherStartupStage::Connecting,EAetherStartupStage::WaitingForBackend,
        EAetherStartupStage::ReadingStorage,EAetherStartupStage::Auditing,EAetherStartupStage::Restoring,EAetherStartupStage::WorldReady,
        EAetherStartupStage::Ready,EAetherStartupStage::Failed,EAetherStartupStage::Cancelled})
        TestFalse(TEXT("Every displayed phase has a title"),UAetherStartupOverlay::StageTitle(Stage).IsEmpty());
    for(const auto Failure:{EAetherStartupFailure::InvalidConfiguration,EAetherStartupFailure::BackendConflict,
        EAetherStartupFailure::BackendDrainTimedOut,EAetherStartupFailure::StorageOpenFailed,EAetherStartupFailure::StorageAuditFailed,
        EAetherStartupFailure::WorldRestoreFailed,EAetherStartupFailure::ProfileUnavailable,EAetherStartupFailure::NetworkFailure,EAetherStartupFailure::TravelFailure})
        TestFalse(TEXT("Bounded failure codes have an explanation"),UAetherStartupOverlay::FailureMessage(Failure).IsEmpty());
    for(const auto Issue:{EAetherStartupRouteIssue::FrontendUnavailable,EAetherStartupRouteIssue::PlayableUnavailable,
        EAetherStartupRouteIssue::NoRetryTarget,EAetherStartupRouteIssue::NoLocalWorld,EAetherStartupRouteIssue::Busy})
        TestFalse(TEXT("Unavailable routes have a local explanation"),UAetherStartupOverlay::RouteMessage(Issue).IsEmpty());

    auto* Client=NewObject<UAetherStartupClient>(GI);
    auto* Host=NewObject<UAetherStartupPresentationSubsystem>(GI);Host->Client=Client;
    Client->OnChanged.AddUObject(Host,&UAetherStartupPresentationSubsystem::ViewChanged);
    Host->bViewDirty=false;Client->OnChanged.Broadcast();TestTrue(TEXT("Provider event marks presentation dirty"),Host->bViewDirty);
    Host->Deinitialize();
    TestFalse(TEXT("Shutdown removes provider subscription"),Client->OnChanged.IsBoundToObject(Host));
    Host->bViewDirty=false;Client->OnChanged.Broadcast();
    TestFalse(TEXT("Late provider event cannot recreate deinitialized UI"),Host->bViewDirty);
    TestFalse(TEXT("Deinitialized host is not tickable"),Host->IsTickable());
    return true;
}
#endif
