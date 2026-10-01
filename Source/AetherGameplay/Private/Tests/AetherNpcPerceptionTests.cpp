#include "Misc/AutomationTest.h"
#include "AI/AetherNpcPerception.h"
#include "AI/AetherNpcPerceptionDefinitions.h"
#include "Combat/AetherCombat.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNpcPerceptionTest,"Aether.AI.Perception.SampledSightAndOwnedMemory",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNpcPerceptionTest::RunTest(const FString&)
{
    const auto* Canonical=FAetherNpcPerceptionDefinitions::Get().ForFighter(TEXT("ShieldGuard"));
    if(!TestNotNull(TEXT("Production perception policy"),Canonical))return false;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated perception world"),World))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    const auto Spawn=[&](EAetherFighter Fighter,FVector Position)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* C=World->SpawnActor<AAetherCharacter>(Position,FRotator::ZeroRotator,Params);
        if(!C)return C;
        C->Fighter=Fighter;C->bUseBasicAssets=true;C->SkillLoadoutId=TEXT("MeleeOnly");C->Home=Position;
        C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());C->AbilitySystem->InitAbilityActorInfo(C,C);C->SetVitals(100,100,100);return C;
    };
    auto* Npc=Spawn(EAetherFighter::ShieldGuard,FVector(0,0,88));
    auto* OtherNpc=Spawn(EAetherFighter::ShieldGuard,FVector(0,500,88));
    auto* Target=Spawn(EAetherFighter::Player,FVector(600,0,88));
    auto* DepartingTarget=Spawn(EAetherFighter::Player,FVector(5000,0,88));
    auto* First=World->SpawnActor<AAIController>();auto* Second=World->SpawnActor<AAIController>();
    if(!TestNotNull(TEXT("First observer"),Npc)||!TestNotNull(TEXT("Independent observer"),OtherNpc)||!TestNotNull(TEXT("Hostile target"),Target)||
        !TestNotNull(TEXT("Lifecycle target"),DepartingTarget)||!TestNotNull(TEXT("First controller"),First)||!TestNotNull(TEXT("Second controller"),Second))return false;
    First->Possess(Npc);Second->Possess(OtherNpc);
    Target->DispatchBeginPlay();DepartingTarget->DispatchBeginPlay();
    if(!TestTrue(TEXT("Targets have entered their real playable lifecycle"),Target->HasActorBegunPlay()&&DepartingTarget->HasActorBegunPlay()))return false;
    auto* Wall=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);Wall->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(25,100,5));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);Box->RegisterComponent();Wall->SetActorLocation(FVector(300,0,128));
    World->SetBegunPlay(true);
    const auto Advance=[&](double Seconds){World->Tick(LEVELTICK_TimeOnly,float(Seconds));};
    Npc->BaseEyeHeight=72;Target->BaseEyeHeight=96;
    auto& Sight=Npc->EnemyPerception;auto View=Sight.Observe(*Npc,*Canonical);
    TestTrue(TEXT("Live configured eyes see over a blocker at the former fixed +40 height"),View.bVisible&&View.Target.Get()==Target);
    Npc->BaseEyeHeight=Target->BaseEyeHeight=40;Advance(Canonical->SampleIntervalSeconds+.01);
    View=Sight.Observe(*Npc,*Canonical);
    TestTrue(TEXT("Changing actual pawn eyes affects the next sight query"),!View.bVisible&&View.Target.Get()==Target);
    Npc->BaseEyeHeight=72;Target->BaseEyeHeight=96;Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    auto Policy=*Canonical;Policy.Id=TEXT("Fixture.Cadence");Policy.SampleIntervalSeconds=1;Policy.ObservationFreshnessSeconds=1;Policy.MemorySeconds=3;
    View=Sight.Observe(*Npc,Policy);const FVector FirstPosition=View.LastSeenPosition;
    Target->SetActorLocation(FVector(650,0,88));Advance(.5);
    View=Sight.Observe(*Npc,Policy);
    TestTrue(TEXT("Between authored samples the observation never reads moving target coordinates"),View.bVisible&&View.LastSeenPosition==FirstPosition);
    Advance(.6);View=Sight.Observe(*Npc,Policy);
    TestTrue(TEXT("Next data-timed sample records the newly visible position"),View.bVisible&&View.LastSeenPosition==Target->GetActorLocation());
    const FVector Remembered=View.LastSeenPosition;const double SeenAt=View.LastSeenAt;
    Box->SetBoxExtent(FVector(25,100,160));Wall->SetActorLocation(FVector(300,0,160));Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Advance(1.1);View=Sight.Observe(*Npc,Policy);
    TestTrue(TEXT("Failed visibility sample immediately leaves the visible state"),View.Target.Get()==Target&&!View.bVisible);
    auto OtherView=OtherNpc->EnemyPerception.Observe(*OtherNpc,Policy);
    TestTrue(TEXT("Another observer owns independent clear-line history"),OtherView.bVisible&&OtherView.Target.Get()==Target);
    Target->SetActorLocation(FVector(700,40,88));Advance(1.1);View=Sight.Observe(*Npc,Policy);
    TestTrue(TEXT("Occluded motion cannot refresh position or memory deadline"),!View.bVisible&&View.LastSeenPosition==Remembered&&View.LastSeenAt==SeenAt);
    Advance(1.1);View=Sight.Observe(*Npc,Policy);
    TestFalse(TEXT("Authored memory deadline expires without hidden tracking"),View.Target.IsValid());
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Policy=*Canonical;Policy.Id=TEXT("Fixture.ShortSight");Policy.SightRadiusCm=400;
    TestFalse(TEXT("A shorter valid data radius rejects the target"),Sight.Observe(*Npc,Policy).Target.IsValid());
    Policy.Id=TEXT("Fixture.LongSight");Policy.SightRadiusCm=800;
    TestTrue(TEXT("A different valid data radius admits the same target"),Sight.Observe(*Npc,Policy).bVisible);
    Policy.Id=TEXT("Fixture.TargetHome");Policy.TargetHomeRadiusCm=650;
    TestFalse(TEXT("Target home envelope is independent of sight radius"),Sight.Observe(*Npc,Policy).Target.IsValid());
    Policy.Id=TEXT("Fixture.SelfLeash");Policy.TargetHomeRadiusCm=750;Policy.SelfLeashRadiusCm=800;
    Npc->SetActorLocation(FVector(900,0,88));
    TestFalse(TEXT("Observer outside its authored leash cannot acquire a nearby target"),Sight.Observe(*Npc,Policy).Target.IsValid());
    Npc->SetActorLocation(FVector(0,0,88));
    TestTrue(TEXT("Returning inside leash can perform a fresh observation"),Sight.Observe(*Npc,Policy).bVisible);
    Target->SetVitals(0,100,100);
    TestFalse(TEXT("Dead target is invalidated before waiting for another sample"),Sight.Observe(*Npc,Policy).Target.IsValid());
    Target->SetVitals(100,100,100);Sight.Reset();
    TestTrue(TEXT("Fresh live target can be reacquired"),Sight.Observe(*Npc,*Canonical).bVisible);

    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Npc->SetVitals(0,100,100);Npc->SetVitals(100,100,100);
    TestFalse(TEXT("Actual death cancellation clears observation even if revival precedes another Think"),Sight.Observe(*Npc,*Canonical).Target.IsValid());
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);Sight.Reset();
    TestTrue(TEXT("Observer reacquires after its new live observation"),Sight.Observe(*Npc,*Canonical).bVisible);
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Npc->ResetCombat();
    TestFalse(TEXT("Explicit combat reset cannot preserve a target hidden across the reset"),Sight.Observe(*Npc,*Canonical).Target.IsValid());
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);Sight.Reset();
    TestTrue(TEXT("Fresh observation exists before control replacement"),Sight.Observe(*Npc,*Canonical).bVisible);

    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Second->UnPossess();Second->Possess(Npc);
    TestFalse(TEXT("A replacement controller cannot inherit prior visible or remembered target"),Sight.Observe(*Npc,*Canonical).Target.IsValid());
    TestFalse(TEXT("Uncontrolled former observer loses its own history"),OtherNpc->EnemyPerception.Observe(*OtherNpc,*Canonical).Target.IsValid());
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);Target->SetActorLocation(FVector(600,0,88));Sight.Reset();
    Npc->Think(.1f); // 真实唯一 Think 必须先采样；随后查询不能把测试时挪动当成已看见。
    Target->SetActorLocation(FVector(1000,0,88));View=Sight.Observe(*Npc,*Canonical);
    TestTrue(TEXT("Production Think really populated the sampled observation"),View.bVisible&&View.LastSeenPosition==FVector(600,0,88));
    Npc->Fighter=EAetherFighter(255);Npc->bBlocking=Npc->bWindingUp=true;Npc->SteeringDirection=FVector::ForwardVector;Npc->Think(.1f);
    TestTrue(TEXT("Missing explicit fighter policy clears outgoing AI intents"),!Npc->bBlocking&&!Npc->bWindingUp&&Npc->SteeringDirection.IsNearlyZero());
    Npc->Fighter=EAetherFighter::ShieldGuard;Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    TestFalse(TEXT("Missing policy also cleared the old observation"),Sight.Observe(*Npc,*Canonical).Target.IsValid());
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);Target->SetVitals(0,100,100);
    Npc->Home=Npc->GetActorLocation()+FVector(Canonical->HomeArrivalRadiusCm*.5,0,0);Npc->NextSteeringAt=0;Npc->Think(.1f);
    TestEqual(TEXT("Within authored home arrival tolerance Think does not request navigation"),Npc->NextSteeringAt,0.f);
    Npc->Home=Npc->GetActorLocation()+FVector(Canonical->HomeArrivalRadiusCm*1.5,0,0);Npc->Think(.1f);
    TestTrue(TEXT("Outside authored home tolerance Think requests its real safe-navigation path"),Npc->NextSteeringAt>Npc->CombatTime());
    // 上一断言只证明导航请求发生，不把无地面的隔离世界声称为路线可达。
    Policy.Id.Reset();TestFalse(TEXT("Invalid data cannot reuse previous observation"),Sight.Observe(*Npc,Policy).Target.IsValid());
    Npc->Home=Npc->GetActorLocation();Target->SetVitals(100,100,100);Sight.Reset();
    TestTrue(TEXT("Target restored for destruction boundary"),Sight.Observe(*Npc,*Canonical).bVisible);
    Target->Destroy();TestFalse(TEXT("Destroyed weak target cannot survive memory"),Sight.Observe(*Npc,*Canonical).Target.IsValid());
    DepartingTarget->SetActorLocation(FVector(600,0,88));Sight.Reset();
    TestTrue(TEXT("Still-live departure fixture is first visibly acquired"),Sight.Observe(*Npc,*Canonical).Target.Get()==DepartingTarget);
    DepartingTarget->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
    if(!TestTrue(TEXT("EndPlay fixture remains alive and un-destroyed but has left play"),IsValid(DepartingTarget)&&!DepartingTarget->IsActorBeingDestroyed()&&
        DepartingTarget->Alive()&&!DepartingTarget->HasActorBegunPlay()))return false;
    TestFalse(TEXT("RemovedFromWorld invalidates remembered target before cadence expiry"),Sight.Observe(*Npc,*Canonical).Target.IsValid());
    Advance(Canonical->SampleIntervalSeconds+.01);
    TestFalse(TEXT("A later scan cannot reacquire the ended but uncollected actor"),Sight.Observe(*Npc,*Canonical).Target.IsValid());
    return true;
}
#endif
