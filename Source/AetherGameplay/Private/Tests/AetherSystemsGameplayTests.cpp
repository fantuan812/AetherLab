#include "Misc/AutomationTest.h"
#include "Combat/AetherCombat.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Framework/AetherProgression.h"
#include "Effects/AetherBuffRuntime.h"
#include "Inventory/AetherResourceGate.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Misc/ScopeExit.h"
#include "Persistence/AetherManualWorldSave.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
AAetherCharacter* MakeBasic(UWorld* W)
{
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* C=W->SpawnActor<AAetherCharacter>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
    C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());C->AbilitySystem->InitAbilityActorInfo(C,C);C->SkillLoadoutId=TEXT("FireCaster");C->GrantSpells();C->SetVitals(100,100,100);return C;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSystemsCommitTest,"Aether.Systems.Runtime.ReentrantCostCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSystemsCommitTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);if(!W)return false;
    ON_SCOPE_EXIT {W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);};
    auto* C=MakeBasic(W);bool Cancelled=false;
    const auto Handle=C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).AddLambda([&](const FOnAttributeChangeData& Change){
        if(!Cancelled&&Change.NewValue<Change.OldValue){Cancelled=true;C->CancelActions();}
    });
    C->TrySkill(TEXT("Fire.Ignite"));
    C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).Remove(Handle);
    TestTrue(TEXT("Cancellation occurs synchronously during debit"),Cancelled);
    TestEqual(TEXT("Refund occurs once on the same life"),C->Mana(),100.f);
    int32 Count=0;for(TActorIterator<AAetherProjectile> It(W);It;++It)++Count;
    TestEqual(TEXT("Cancelled commit cannot spawn projectile"),Count,0);
    TestFalse(TEXT("Cancelled execution identity is cleared"),C->CastExecutionId.IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSystemsProjectileTest,"Aether.Systems.Runtime.ProjectilePathBound",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSystemsProjectileTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);if(!W)return false;
    ON_SCOPE_EXIT {W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);};
    for(double Range:{1800.,1900.,2000.})
    {
        auto* P=W->SpawnActor<AAetherProjectile>();P->MaxPathCm=Range;P->VelocityCm=FVector(1300,0,0);P->HeatJ=60000;
        P->Tick(2.f);
        TestTrue(TEXT("Last large frame is truncated at path bound"),FMath::IsNearlyEqual(P->TravelledCm,Range,.01));
        TestTrue(TEXT("World location cannot advance past configured range"),FMath::IsNearlyEqual(P->GetActorLocation().Size(),Range,.01));
        TestTrue(TEXT("Range exhaustion ends projectile"),P->IsActorBeingDestroyed());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSystemsGateTest,"Aether.Systems.Runtime.BoundedQueueAndCapturedDefense",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSystemsGateTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);if(!W)return false;
    ON_SCOPE_EXIT {W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);};
    auto* C=MakeBasic(W);auto* Gate=C->ResourceGate.Get();
    TestTrue(TEXT("Establish life"),Gate->BeginFullRespawn(TEXT("QueueFixture"))&&Gate->FinishRecovery());
    for(int32 FPS:{30,60,120})
    {
        FGuid Command=FGuid::NewGuid();FAetherResourceStateV10 Before;TestTrue(TEXT("Reserve resources"),Gate->Reserve(Command,Before));
        double Sum=0,Timeline=0;
        for(int32 Frame=0;Frame<FPS*2;++Frame)
        {
            FAetherResourceAdvanceInterval I;I.Identity.LifeId=Before.LifeId;I.Start=Timeline;Timeline+=1./FPS;I.End=Timeline;
            Gate->DeferInterval(I,[&Sum](double Dt){Sum+=Dt;});
        }
        TestEqual(TEXT("Two seconds of frames occupy one queue entry"),Gate->Inspect().DeferredCount,1);
        TestTrue(TEXT("Known rollback releases reservation"),Gate->CancelKnownUncommitted(Command));
        Gate->TickComponent(.01f,LEVELTICK_All,nullptr);
        TestTrue(TEXT("Merged duration conserved"),FMath::IsNearlyEqual(Sum,2.,1.e-8));
    }
    FGuid Command=FGuid::NewGuid();FAetherResourceStateV10 Before;Gate->Reserve(Command,Before);
    Gate->Defer([C]{C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetGearArmorAttribute(),100);},EAetherEffectEventKind::ProjectionRefresh);
    FDamageEvent Damage;C->TakeDamage(30,Damage,nullptr,nullptr);
    Gate->CancelKnownUncommitted(Command);Gate->TickComponent(.01f,LEVELTICK_All,nullptr);
    TestEqual(TEXT("Deferred hit keeps armor sampled at contact"),C->Health(),70.f);
    C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetGearArmorAttribute(),0);
    Command=FGuid::NewGuid();Gate->Reserve(Command,Before);int32 Steps=0;double Timeline=0;
    for(int32 I=0;I<240;++I)
    {
        FAetherResourceAdvanceInterval Interval;Interval.Identity.LifeId=Before.LifeId;Interval.bPreserveSteps=true;Interval.HazardRate=1;
        Interval.Start=Timeline;Timeline+=1./120;Interval.End=Timeline;Gate->DeferInterval(Interval,[&Steps](double){++Steps;});
    }
    TestEqual(TEXT("Hazard steps share one bounded entry"),Gate->Inspect().DeferredCount,1);Gate->CancelKnownUncommitted(Command);
    Gate->TickComponent(.01f,LEVELTICK_All,nullptr);TestEqual(TEXT("One frame drains only its fixed budget"),Steps,32);
    for(int32 I=0;I<7;++I)Gate->TickComponent(.01f,LEVELTICK_All,nullptr);
    TestEqual(TEXT("No continuous hazard substep lost"),Steps,240);TestFalse(TEXT("Ordinary interval processing does not fault"),Gate->IsFaulted());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSystemsBuffRuntimeTest,"Aether.Systems.Runtime.BuffProjectionCapsTagsAndDeath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSystemsBuffRuntimeTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);if(!W)return false;
    ON_SCOPE_EXIT {W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);};
    auto* C=W->SpawnActor<AAetherFrontierCharacter>();auto* PS=W->SpawnActor<AAetherPlayerState>();
    PS->Profile.CharacterId=TEXT("BuffFixture");C->SetPlayerState(PS);C->BindPersistentAbilities();C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());
    FAetherProfileStateV10 P;P.CharacterId=PS->Profile.CharacterId;FString Why;
    if(!TestTrue(*Why,PS->PublishNativeProfile(P,Why)&&PS->PublishNativeSkills(P,{},Why)))return false;
    if(!TestTrue(TEXT("Resource life starts"),C->ResourceGate->BeginFullRespawn(P.CharacterId)&&C->ResourceGate->FinishRecovery()))return false;
    C->BuffRuntime->TickComponent(.1f,LEVELTICK_All,nullptr);C->SetVitals(60,100,100);
    TestTrue(TEXT("Vitality is an actual projected effect"),C->BuffRuntime->Apply(TEXT("Sample.Vitality"),TEXT("Test"),Why));
    TestEqual(TEXT("Maximum increases by twenty"),C->MaxHealth,120.f);TestEqual(TEXT("Maximum increase grants no healing"),C->Health(),60.f);
    C->SetVitals(110,100,100);TestTrue(TEXT("Positive dispel accepted"),C->BuffRuntime->Dispel(TEXT("Positive"),Why));
    TestEqual(TEXT("Maximum decrease clips excess only"),C->Health(),100.f);TestEqual(TEXT("Original maximum restored"),C->MaxHealth,100.f);
    for(int32 I=0;I<100;++I)TestTrue(TEXT("Repeated publication remains valid"),PS->RebindNativeSkills(Why));
    TestEqual(TEXT("Only one total attribute GE survives"),C->AbilitySystem->GetActiveEffects(FGameplayEffectQuery()).Num(),1);
    TestTrue(TEXT("Silence applies"),C->BuffRuntime->Apply(TEXT("Sample.Silence"),TEXT("Test"),Why));
    TestTrue(TEXT("Silence reaches action policy"),C->QueryAction(EAetherActionKind::Spell)==EAetherActionDenial::Silenced);
    const auto Tag=FGameplayTag::RequestGameplayTag(TEXT("State.Aether.Buff.Silenced"));
    TestTrue(TEXT("Runtime owns GAS tag mirror"),C->AbilitySystem->HasMatchingGameplayTag(Tag));
    C->SetVitals(0,100,100);C->BuffRuntime->TickComponent(.1f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Death clears life effects"),C->BuffRuntime->GetState().Instances.IsEmpty());
    TestFalse(TEXT("Death removes owned GAS tag"),C->AbilitySystem->HasMatchingGameplayTag(Tag));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherElectricalSnapshotTest,"Aether.Systems.Runtime.ElectricalWindowSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherElectricalSnapshotTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);if(!W)return false;
    ON_SCOPE_EXIT {W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);};
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT {GEngine->DestroyWorldContext(W);};
    auto* Immediate=MakeBasic(W);auto* Queued=MakeBasic(W);auto* Gate=Queued->ResourceGate.Get();
    TestTrue(TEXT("Establish electrical life"),Gate->BeginFullRespawn(TEXT("ElectricalFixture"))&&Gate->FinishRecovery());
    FGuid Command=FGuid::NewGuid();FAetherResourceStateV10 Before;
    TestTrue(TEXT("Reserve electrical resources"),Gate->Reserve(Command,Before));
    FReactiveElectricalWindow Window;Window.DurationSeconds=.05;Window.DeliveredJ=700;
    FReactiveElectricalExposure Exposure;Exposure.DeliveredJ=350;Exposure.Source=Immediate;Window.Contributions.Add(Exposure);
    Exposure.Source=Queued;Window.Contributions.Add(Exposure);
    Immediate->Reactive->State.ElectricalWetness01=Queued->Reactive->State.ElectricalWetness01=.8;
    W->SetBegunPlay(true);W->Tick(LEVELTICK_TimeOnly,.1f);
    const float FirstTime=Queued->CombatTime();
    TestTrue(TEXT("World fixture advances the actual combat clock"),FirstTime>0);
    Immediate->ElectricalWindow(Window);Queued->ElectricalWindow(Window);
    Immediate->Reactive->State.ElectricalWetness01=Queued->Reactive->State.ElectricalWetness01=0;
    Immediate->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetGearStormResistAttribute(),80);
    Queued->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetGearStormResistAttribute(),80);
    // The second strong event is outside the first event's three-second cooldown.
    for(int32 I=0;I<31;++I)W->Tick(LEVELTICK_TimeOnly,.1f);
    const float SecondTime=Queued->CombatTime();
    TestTrue(TEXT("Two exposures are actually over three seconds apart"),SecondTime-FirstTime>3);
    Immediate->ElectricalWindow(Window);Queued->ElectricalWindow(Window);
    // Change both defenses and wetness again before any queued event drains.
    Queued->Reactive->State.ElectricalWetness01=1;
    Queued->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetGearStormResistAttribute(),0);
    Gate->CancelKnownUncommitted(Command);Gate->TickComponent(.01f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Queue latency preserves wetness and per-source resistance damage"),FMath::IsNearlyEqual(Queued->Health(),Immediate->Health(),.001f));
    TestTrue(TEXT("Separated windows preserve cooldown at event time"),FMath::IsNearlyEqual(Queued->NextShockStun,SecondTime+3,.001f));
    TestTrue(TEXT("Event time rather than first-drain cooldown is used"),Queued->NextShockStun>FirstTime+6);
    TestTrue(TEXT("Damage bookkeeping preserves last event timestamp"),FMath::IsNearlyEqual(Queued->CombatRuntime->LastDamageAt,SecondTime,.001f));
    TestEqual(TEXT("Both sources in both windows are settled"),Queued->CombatRuntime->DamageReceivedCount,uint64(4));
    TestFalse(TEXT("Electrical queue fully drains"),Gate->IsBlocked());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherManualWorldSaveTest,"Aether.Systems.Persistence.ManualSaveConfirmation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherManualWorldSaveTest::RunTest(const FString&)
{
    for(const auto Code:{EAetherStoreCode::Committed,EAetherStoreCode::Replayed,EAetherStoreCode::Busy,EAetherStoreCode::Unavailable})
    {
        FAetherManualWorldSave Request;TPromise<FAetherWorldCheckpointResult> Promise;int32 Starts=0;
        TestTrue(TEXT("Manual F5 request begins"),Request.Start([&]{++Starts;return Promise.GetFuture();}));
        TestFalse(TEXT("No success feedback before durable confirmation"),Request.Poll().IsSet());
        TestFalse(TEXT("Repeated F5 cannot replace outstanding request"),Request.Start([&]{++Starts;return TFuture<FAetherWorldCheckpointResult>();}));
        TestEqual(TEXT("Exactly one checkpoint requested"),Starts,1);
        FAetherWorldCheckpointResult Result;Result.Code=Code;
        if(Code==EAetherStoreCode::Committed||Code==EAetherStoreCode::Replayed){Result.World=FAetherWorldStateV10();Result.World->Revision=42;}
        Promise.SetValue(MoveTemp(Result));const auto Feedback=Request.Poll();
        TestTrue(TEXT("Completion produces feedback and allows retry"),Feedback.IsSet()&&!Request.IsPending());
        if(Feedback.IsSet())TestEqual(TEXT("Success feedback requires commit/replay proof"),Feedback->Contains(TEXT("修订 42")),Code==EAetherStoreCode::Committed||Code==EAetherStoreCode::Replayed);
        TestFalse(TEXT("Completion feedback is emitted once"),Request.Poll().IsSet());
        TPromise<FAetherWorldCheckpointResult> Retry;
        TestTrue(TEXT("Busy or failure permits a fresh user retry"),Request.Start([&]{return Retry.GetFuture();}));
        FAetherWorldCheckpointResult MissingProof;MissingProof.Code=EAetherStoreCode::Committed;Retry.SetValue(MoveTemp(MissingProof));
        TestTrue(TEXT("Missing committed world is not reported saved"),Request.Poll()->Contains(TEXT("保存未确认")));
    }
    return true;
}
#endif
