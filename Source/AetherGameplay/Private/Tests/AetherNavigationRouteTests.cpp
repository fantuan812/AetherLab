#include "Misc/AutomationTest.h"
#include "Movement/AetherNavigationRoute.h"
#include "Movement/AetherNavigationProbe.h"
#include "Combat/AetherCombat.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationSystemTypes.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FNavPathSharedPtr RoutePath(FVector Start,FVector End,bool Partial=false)
{
    auto Path=MakeShared<FNavigationPath,ESPMode::ThreadSafe>(TArray<FVector>{Start,End});
    Path->SetIsPartial(Partial);Path->MarkReady();return Path;
}
void AcceptResult(FAetherNavigationRoute& Route,ENavigationQueryResult::Type Status,FNavPathSharedPtr Path)
{
    FPathFindingResult Result(Status);Result.Path=MoveTemp(Path);Route.AcceptQueryResult(Result);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNavigationRouteStateTest,"Aether.AI.Navigation.NativeResultLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNavigationRouteStateTest::RunTest(const FString&)
{
    // 直接消费真实FNavigationPath的结果/失效合同；本例不冒称执行了Recast建图或真实查询。
    const FVector Start(0,0,88),End(500,0,88);FVector Waypoint;
    FAetherNavigationRoute Route;
    auto Partial=RoutePath(Start,End,true);
    if(!TestTrue(TEXT("Native partial fixture is ready and current"),Partial->IsValid()&&Partial->IsUpToDate()&&Partial->IsPartial()))return false;
    Partial->EnableRecalculationOnInvalidation(true);
    AcceptResult(Route,ENavigationQueryResult::Success,Partial);
    TestFalse(TEXT("Adopted native paths cannot bypass the owner retry throttle"),Partial->WillRecalculateOnInvalidation());
    TestTrue(TEXT("A partial route follows its available prefix"),Route.TakeWaypoint(Start,90,Waypoint)&&Waypoint==End);
    TestTrue(TEXT("Partial endpoint is a distinct stop state"),!Route.TakeWaypoint(End-FVector(40,0,0),90,Waypoint)&&Route.GetStatus()==EAetherNavigationRouteStatus::PartialFinished);
    TestFalse(TEXT("Partial completion cannot authorize direct destination steering"),Route.AllowsDirectSteering());
    TestTrue(TEXT("Repeated endpoint consumption does not resurrect a path"),!Route.TakeWaypoint(End,90,Waypoint));

    auto Complete=RoutePath(Start,End);
    AcceptResult(Route,ENavigationQueryResult::Success,Complete);
    TestTrue(TEXT("Complete local segment still exposes its waypoint"),Route.TakeWaypoint(Start,90,Waypoint)&&Waypoint==End);
    TestTrue(TEXT("Complete segment completion awaits another query, not an unprojected destination"),!Route.TakeWaypoint(End,90,Waypoint)&&Route.GetStatus()==EAetherNavigationRouteStatus::CompleteSegmentFinished);
    TestFalse(TEXT("A completed local segment is not missing navigation"),Route.AllowsDirectSteering());

    auto Dynamic=RoutePath(Start,End);
    AcceptResult(Route,ENavigationQueryResult::Success,Dynamic);const uint32 BeforeInvalidation=Route.GetRevision();
    Dynamic->Invalidate(); // 调用引擎原生生命周期，未直接伪写helper状态。
    TestTrue(TEXT("Native invalidation is observed before using copied points"),!Route.RefreshValidity()&&Route.GetStatus()==EAetherNavigationRouteStatus::Invalidated);
    TestTrue(TEXT("Invalidation changes route identity"),Route.GetRevision()!=BeforeInvalidation);
    TestFalse(TEXT("Invalidated routes cannot supply a stale waypoint"),Route.TakeWaypoint(Start,90,Waypoint));
    AcceptResult(Route,ENavigationQueryResult::Success,Dynamic);
    TestTrue(TEXT("A stale success result is not adopted again"),Route.GetStatus()==EAetherNavigationRouteStatus::QueryFailed);
    AcceptResult(Route,ENavigationQueryResult::Fail,Complete);
    TestTrue(TEXT("A failed query cannot borrow a valid path pointer"),Route.GetStatus()==EAetherNavigationRouteStatus::QueryFailed&&!Route.TakeWaypoint(Start,90,Waypoint));
    AcceptResult(Route,ENavigationQueryResult::Success,FNavPathSharedPtr());
    TestTrue(TEXT("Success without a path remains failure"),Route.GetStatus()==EAetherNavigationRouteStatus::QueryFailed);
    Route.Clear();TestTrue(TEXT("Lifecycle reset releases old route state"),Route.GetStatus()==EAetherNavigationRouteStatus::Empty);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNavigationRouteSteeringTest,"Aether.AI.Navigation.ResultBeforeCachedSteering",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNavigationRouteSteeringTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated collision world"),World))return false;
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);};
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if(!TestTrue(TEXT("Fixture intentionally has no registered navigation data"),!Nav||!Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)))return false;
    auto* GroundActor=World->SpawnActor<AActor>();auto* Ground=NewObject<UBoxComponent>(GroundActor);
    GroundActor->SetRootComponent(Ground);GroundActor->AddInstanceComponent(Ground);
    Ground->SetBoxExtent(FVector(3000,3000,10));Ground->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Ground->SetCollisionObjectType(ECC_WorldStatic);Ground->SetCollisionResponseToAllChannels(ECR_Block);Ground->RegisterComponent();
    GroundActor->SetActorLocation(FVector(0,0,-10));
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* C=World->SpawnActor<AAetherCharacter>(FVector(0,0,88),FRotator::ZeroRotator,Params);
    if(!TestNotNull(TEXT("Production character"),C))return false;
    auto* Movement=C->GetCharacterMovement();Movement->SetMovementMode(MOVE_Walking);
    const FVector Start=C->GetActorLocation(),Goal=Start+FVector(1000,0,0);const float Now=C->CombatTime(),Deadline=Now+.75f;
    FHitResult Support;if(!TestTrue(TEXT("Physical forward step is actually clear"),AetherNavigationProbe::IsLocalStepClear(*C,FVector(150,0,0),Support)))return false;
    const auto Install=[&](const FNavPathSharedPtr& Path)
    {
        C->NavigationGoal=Goal;C->NextPathAt=Deadline;C->NextSteeringAt=Now+1;C->SteeringDirection=FVector::ForwardVector;
        AcceptResult(C->NavigationRoute,ENavigationQueryResult::Success,Path);
    };
    auto Native=RoutePath(Start,Goal);Install(Native);
    TestTrue(TEXT("A live native route can use its locally safe cached steering"),C->SafeMoveDirection(Goal).X>.99);
    Native->Invalidate();
    TestTrue(TEXT("Native invalidation stops an otherwise clear cached forward direction"),C->SafeMoveDirection(Goal).IsNearlyZero());
    TestTrue(TEXT("Production steering reports native invalidation"),C->NavigationRoute.GetStatus()==EAetherNavigationRouteStatus::Invalidated);
    for(int32 I=0;I<4;++I)TestTrue(TEXT("Repeated calls wait instead of synchronously requerying each frame"),C->SafeMoveDirection(Goal).IsNearlyZero()&&C->NextPathAt==Deadline);

    Install(RoutePath(Start,Start+FVector(40,0,0),true));
    TestTrue(TEXT("Partial endpoint stops before the cached direction can escape the prefix"),C->SafeMoveDirection(Goal).IsNearlyZero()&&C->NavigationRoute.GetStatus()==EAetherNavigationRouteStatus::PartialFinished);
    TestEqual(TEXT("Partial completion keeps the scheduled query deadline"),C->NextPathAt,Deadline);
    Install(RoutePath(Start,Start+FVector(40,0,0)));
    TestTrue(TEXT("Complete segment endpoint does not advance toward the raw destination"),C->SafeMoveDirection(Goal).IsNearlyZero()&&C->NavigationRoute.GetStatus()==EAetherNavigationRouteStatus::CompleteSegmentFinished);
    TestEqual(TEXT("Complete segment completion is also throttled"),C->NextPathAt,Deadline);
    Install(RoutePath(Start,Goal));
    AcceptResult(C->NavigationRoute,ENavigationQueryResult::Fail,RoutePath(Start,Goal));
    TestTrue(TEXT("A failed query cannot reuse a safe cached direction"),C->SafeMoveDirection(Goal).IsNearlyZero());
    Install(RoutePath(Start,Goal));
    const FVector ChangedGoal=Start+FVector(0,1000,0);
    TestTrue(TEXT("A changed goal invalidates the prior route while waiting for its deadline"),C->SafeMoveDirection(ChangedGoal).IsNearlyZero()&&C->NextPathAt==Deadline);

    // 真实Query入口在deadline到期后重新观测无Nav；保留旧demo的局部steering，而非永久停步。
    C->NextPathAt=Now;
    TestTrue(TEXT("Missing navigation preserves physically guarded movement after a real retry"),C->SafeMoveDirection(Goal).X>.99&&C->NavigationRoute.GetStatus()==EAetherNavigationRouteStatus::MissingNavigation);
    TestEqual(TEXT("A real query establishes the existing retry interval"),C->NextPathAt,Deadline);
    TestTrue(TEXT("Missing-Nav steering can follow a changed goal without waiting for Nav"),C->SafeMoveDirection(ChangedGoal).Y>.99&&C->NextPathAt==Deadline);

    // 同一个真实地面组件成为融冰支撑；即使原路线已失败，也先执行既有撤离。
    auto* Ice=NewObject<UReactiveBodyComponent>(GroundActor);GroundActor->AddInstanceComponent(Ice);
    Ice->bParticipatesInSimulation=false;Ice->bIceControlsPawnCollision=true;Ice->IceSupport=EReactiveIceSupport::Thawing;Ice->RegisterComponent();
    const float Gap=UCharacterMovementComponent::MAX_FLOOR_DIST;
    Movement->ComputeFloorDist(Start+FVector(0,0,Gap),Gap*2,Gap*2,Movement->CurrentFloor,C->GetCapsuleComponent()->GetScaledCapsuleRadius(),nullptr);
    if(!TestTrue(TEXT("Escape fixture uses the actual current support actor"),Movement->CurrentFloor.HitResult.GetActor()==GroundActor))return false;
    C->NavigationRoute.Clear(EAetherNavigationRouteStatus::ProjectionFailed);C->NavigationGoal=Goal;
    TestTrue(TEXT("Thawing support escape precedes projection-failure waiting"),!C->SafeMoveDirection(Goal).IsNearlyZero()&&C->NavigationRoute.GetStatus()==EAetherNavigationRouteStatus::EscapingIce);
    TestEqual(TEXT("Escape does not remove the query throttle"),C->NextPathAt,Deadline);
    Ice->IceSupport=EReactiveIceSupport::Bearing;
    TestTrue(TEXT("Finishing escape cannot revive the old raw-goal route"),C->SafeMoveDirection(Goal).IsNearlyZero()&&C->NextPathAt==Deadline);
    C->ResetNavigation();
    TestTrue(TEXT("Explicit lifecycle reset drops cached direction and query state"),C->NavigationRoute.GetStatus()==EAetherNavigationRouteStatus::Empty&&C->SteeringDirection.IsNearlyZero()&&C->NextPathAt==0&&C->NextSteeringAt==0);
    return true;
}
#endif
