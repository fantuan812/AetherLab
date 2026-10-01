#include "Misc/AutomationTest.h"
#include "World/AetherEncounters.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Framework/AetherFrontierMode.h"
#include "Definitions/AetherRules.h"
#include "Equipment/AetherElementDamage.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherEncounterLifecycleTest,"Aether.World.Encounter.DefeatEvidenceSurvivesCleanup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherEncounterLifecycleTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated authority world"),World))return false;
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);};
    auto* Director=World->SpawnActor<AAetherEncounterDirector>();
    auto* Mode=World->SpawnActor<AAetherFrontierMode>();
    if(!TestNotNull(TEXT("Production encounter director"),Director)||!TestNotNull(TEXT("Production spawn mode"),Mode))return false;
    const auto Initialize=[](AAetherFrontierCharacter* Enemy)
    {
        Enemy->SetActorEnableCollision(false);
        Enemy->AbilitySystem->AddAttributeSetSubobject(Enemy->Attributes.Get());
        Enemy->AbilitySystem->InitAbilityActorInfo(Enemy,Enemy);
        Enemy->SetVitals(Enemy->MaxHealth,100,100);
    };
    const auto Spawn=[&]()
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Enemy=World->SpawnActor<AAetherFrontierCharacter>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
        if(Enemy){Enemy->Fighter=EAetherFighter::ShieldGuard;Initialize(Enemy);}return Enemy;
    };
    const auto DefeatWithHeat=[&](AAetherFrontierCharacter* Enemy)
    {
        // 元素命中走现有角色 -> CombatRuntime -> GAS 结算，测试不复制伤害/生命公式。
        FDamageEvent Heat;Heat.DamageTypeClass=UAetherHeatExposureDamage::StaticClass();
        TestTrue(TEXT("Authoritative heat damage is applied"),Enemy->TakeDamage(Enemy->MaxHealth*2,Heat,nullptr,nullptr)>0);
        TestTrue(TEXT("GAS health confirms defeat"),Enemy->Health()<=0&&!Enemy->Alive());
    };
    const auto BeginBoss=[&](AAetherFrontierCharacter* Enemy)
    {
        Director->ReleaseEnemies(Director->AbbeyEnemies,Director->AbbeyLifecycle);
        Director->Abbey=FAetherEncounterRun();Director->Abbey.Definition=TEXT("Abbey");
        Director->Abbey.Instance=FGuid::NewGuid();Director->Abbey.Phase=EAetherEncounterPhase::Boss;
        Director->AbbeyEnemies.Add(Enemy);Director->WatchEnemies(Director->AbbeyEnemies);
    };

    auto* MissingBoss=Spawn();if(!TestNotNull(TEXT("Live boss fixture"),MissingBoss))return false;
    BeginBoss(MissingBoss);TestTrue(TEXT("Live enemy destruction uses production lifecycle"),MissingBoss->Destroy());
    Director->UpdateRun(Director->Abbey,Director->AbbeyEnemies,.1f,Director->EmptySinceAbbey);
    TestTrue(TEXT("Lost live boss fails instead of succeeding"),Director->Abbey.Phase==EAetherEncounterPhase::Failed);
    TestTrue(TEXT("Failed encounter has no settled participant or surviving partial wave"),Director->Abbey.Settled.IsEmpty()&&Director->AbbeyEnemies.IsEmpty());

    auto* DefeatedBoss=Spawn();if(!TestNotNull(TEXT("Defeated boss fixture"),DefeatedBoss))return false;
    BeginBoss(DefeatedBoss);DefeatWithHeat(DefeatedBoss);
    TestTrue(TEXT("Real corpse destruction succeeds"),DefeatedBoss->Destroy());
    Director->AbbeyEnemies[0]=nullptr; // 模拟 GC 清空 UPROPERTY；判断不能依赖尸体对象继续存在。
    Director->UpdateRun(Director->Abbey,Director->AbbeyEnemies,.1f,Director->EmptySinceAbbey);
    TestTrue(TEXT("Confirmed defeat remains a success after corpse cleanup"),Director->Abbey.Phase==EAetherEncounterPhase::Succeeded);

    auto* LiveBoss=Spawn();if(!TestNotNull(TEXT("New boss fixture"),LiveBoss))return false;
    BeginBoss(LiveBoss);
    TestTrue(TEXT("New wave cannot inherit the previous slot's defeat"),Director->AbbeyLifecycle.State(Director->AbbeyEnemies)==EAetherEncounterWaveState::Active);
    Director->EnemyEndPlay(LiveBoss,EEndPlayReason::RemovedFromWorld);
    DefeatWithHeat(LiveBoss);Director->EnemyDestroyed(LiveBoss);
    TestTrue(TEXT("A later callback cannot rewrite a live-unload fact as victory"),Director->AbbeyLifecycle.State(Director->AbbeyEnemies)==EAetherEncounterWaveState::Unavailable);

    auto* CampEnemy=Spawn();if(!TestNotNull(TEXT("Camp fixture"),CampEnemy))return false;
    auto& Camp=Director->Camps.AddDefaulted_GetRef();Camp.Definition=TEXT("LifecycleFixture");
    Camp.bSpawned=true;Camp.Instance=FGuid::NewGuid();Camp.Enemies.Add(CampEnemy);Director->WatchEnemies(Camp.Enemies);
    TestFalse(TEXT("Living camp is not reward eligible"),Camp.CanCreateClearReward());
    CampEnemy->Destroy();
    TestFalse(TEXT("Despawned live enemy is not a clear reward"),Camp.CanCreateClearReward());
    TestFalse(TEXT("Production availability transition rejects the missing member"),Director->RefreshCampAvailability(Camp));
    TestTrue(TEXT("Unavailable camp releases its partial batch and cannot settle its instance"),Camp.bSpawnFailed&&!Camp.bSpawned&&Camp.Enemies.IsEmpty()&&!Camp.Instance.IsValid());

    FAetherEncounterRule Rule;Rule.Types={uint8(EAetherFighter::ShieldGuard)};
    TestTrue(TEXT("Production camp can retry an unavailable batch"),Director->SpawnCamp(*Mode,Camp,Rule));
    if(Camp.Enemies.Num()!=1)return false;Initialize(Camp.Enemies[0]);
    DefeatWithHeat(Camp.Enemies[0]);Camp.Enemies[0]->Destroy();Camp.Enemies[0]=nullptr;
    TestTrue(TEXT("Real camp defeat survives corpse cleanup"),Camp.CanCreateClearReward());
    TestTrue(TEXT("Confirmed corpse cleanup keeps camp available"),Director->RefreshCampAvailability(Camp));
    Camp.bRewardCreated=true;
    TestFalse(TEXT("Reward creation remains single-shot"),Camp.CanCreateClearReward());
    Director->ReleaseEnemies(Camp.Enemies,Camp.Lifecycle);Camp.bSpawned=false;
    TestTrue(TEXT("Production respawn starts a fresh batch"),Director->SpawnCamp(*Mode,Camp,Rule));
    if(Camp.Enemies.Num()!=1)return false;Initialize(Camp.Enemies[0]);
    TestFalse(TEXT("Respawned live member cannot inherit the old slot's defeat"),Camp.CanCreateClearReward());

    auto* Survivor=Spawn();if(!TestNotNull(TEXT("Mixed wave survivor"),Survivor))return false;
    Director->AbbeyEnemies={nullptr,Survivor};Director->AbbeyLifecycle.Reset();
    AAetherFrontierCharacter* Active=nullptr;
    TestTrue(TEXT("Missing member invalidates a partially living wave"),Director->AbbeyLifecycle.State(Director->AbbeyEnemies,&Active)==EAetherEncounterWaveState::Unavailable);
    Director->AbbeyEnemies.Reset();Director->Abbey.Phase=EAetherEncounterPhase::Front;
    Director->UpdateRun(Director->Abbey,Director->AbbeyEnemies,.1f,Director->EmptySinceAbbey);
    TestTrue(TEXT("An empty required wave fails closed"),Director->Abbey.Phase==EAetherEncounterPhase::Failed);

    Director->Relay=FAetherEncounterRun();Director->Relay.Definition=TEXT("Relay");
    Director->Relay.Phase=EAetherEncounterPhase::Channel;Director->Relay.Instance=FGuid::NewGuid();
    Director->UpdateRun(Director->Relay,Director->RelayEnemies,.1f,Director->EmptySinceRelay);
    TestTrue(TEXT("Restored Relay channel legitimately has no enemy wave"),Director->Relay.Phase==EAetherEncounterPhase::Channel);
    Director->Relay.Progress=3;
    Director->UpdateRun(Director->Relay,Director->RelayEnemies,.1f,Director->EmptySinceRelay);
    TestTrue(TEXT("Legitimate Relay channel can finish"),Director->Relay.Phase==EAetherEncounterPhase::Succeeded);
    return true;
}
#endif
