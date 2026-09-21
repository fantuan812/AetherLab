#include "Misc/AutomationTest.h"
#include "Combat/AetherCombat.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Skills/AetherSkillDefinitions.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "Components/BoxComponent.h"
#include "ReactiveWorldSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSkillIdentityTest,"Aether.V10.Skills.GASStableIdentityAndRankExecution",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSkillIdentityTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated test world"),World))return false;
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false); };
    auto* C=World->SpawnActor<AAetherCharacter>();
    if(!TestNotNull(TEXT("Real character and GAS"),C))return false;
    // 隔离世界没有执行整张地图 BeginPlay，显式注册生产角色自带的属性集。
    C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());
    C->AbilitySystem->InitAbilityActorInfo(C,C);
    C->GrantSpells();
    const auto& D=FAetherSkillDefinitionsV10::Get();
    auto* Fire=AetherSkillBinding::Find(*C->AbilitySystem,TEXT("Fire.Ignite"));
    auto* Storm=AetherSkillBinding::Find(*C->AbilitySystem,TEXT("Storm.Strike"));
    if(!TestNotNull(TEXT("Fire spec"),Fire)||!TestNotNull(TEXT("Same GA distinct storm spec"),Storm))return false;
    TestTrue(TEXT("Distinct handles even for the same GA"),Fire->Handle!=Storm->Handle);
    TestEqual(TEXT("Storm starts at rank one, not old spell number"),Storm->Level,1);
    const auto FireHandle=Fire->Handle;
    Fire->Level=3;Fire->InputID=3;
    TestEqual(TEXT("Changing rank/input never changes identity"),AetherSkillBinding::Identify(*Fire),FString(TEXT("Fire.Ignite")));
    const auto Count=C->AbilitySystem->GetActivatableAbilities().Num();
    C->GrantSpells();
    Fire=AetherSkillBinding::Find(*C->AbilitySystem,TEXT("Fire.Ignite"));
    if(!TestNotNull(TEXT("Still exactly one fire spec"),Fire))return false;
    TestTrue(TEXT("Repeated grant preserves handle"),Fire->Handle==FireHandle);
    TestEqual(TEXT("Repeated grant preserves rank"),Fire->Level,3);
    TestEqual(TEXT("Repeated grant cannot stack abilities"),C->AbilitySystem->GetActivatableAbilities().Num(),Count);
    FGameplayAbilitySpec Ambiguous=*Fire;
    Ambiguous.GetDynamicSpecSourceTags().AddTag(AetherSkillBinding::TagFor(TEXT("Water.Draw")));
    TestTrue(TEXT("Multiple identity tags rejected"),AetherSkillBinding::Identify(Ambiguous).IsEmpty());
    FGameplayAbilitySpec Untagged(UAetherSpellAbility::StaticClass(),4,0);
    TestTrue(TEXT("Level/input cannot stand in for missing identity"),AetherSkillBinding::Identify(Untagged).IsEmpty());
    C->SetVitals(100,100,100);
    TestTrue(TEXT("Shared GA activates tagged fire at rank three"),C->TrySkill(TEXT("Fire.Ignite")));
    TestEqual(TEXT("Actual cost comes from rank three"),C->Mana(),76.f);
    int32 Projectiles=0;
    for(TActorIterator<AAetherProjectile> It(World);It;++It)
    {++Projectiles;TestEqual(TEXT("Actual projectile carries upgraded heat"),It->HeatJ,90000.);}
    TestEqual(TEXT("Exactly one accepted cast"),Projectiles,1);
    TestTrue(TEXT("Actual cast lock comes from same rank"),FMath::IsNearlyEqual(C->CastLockUntil-C->CombatTime(),float(D.Effect(TEXT("Fire.Ignite"),3)->Cooldown)));
    TestFalse(TEXT("Cooldown rejects duplicate cast"),C->TrySkill(TEXT("Fire.Ignite")));
    TestEqual(TEXT("Rejected cast spends no mana"),C->Mana(),76.f);
    C->ResetCombat();C->SetVitals(100,10,100);
    TestFalse(TEXT("Insufficient mana rejects upgraded cast"),C->TrySkill(TEXT("Fire.Ignite")));
    TestEqual(TEXT("Mana unchanged on failure"),C->Mana(),10.f);
    Fire=AetherSkillBinding::Find(*C->AbilitySystem,TEXT("Fire.Ignite"));Fire->Level=4;
    C->SetVitals(100,100,100);
    TestFalse(TEXT("Unsupported rank cannot cast"),C->TrySkill(TEXT("Fire.Ignite")));
    TestEqual(TEXT("Unsupported rank spends no mana"),C->Mana(),100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSkillWaterTest,"Aether.V10.Skills.UpgradedWaterConservationAndRejectedInjection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSkillWaterTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated reactive world"),World))return false;
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false); };
    auto* C=World->SpawnActor<AAetherCharacter>();
    if(!TestNotNull(TEXT("Caster"),C))return false;
    C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());
    C->AbilitySystem->InitAbilityActorInfo(C,C);C->GrantSpells();C->SetVitals(100,100,100);
    auto* Spec=AetherSkillBinding::Find(*C->AbilitySystem,TEXT("Water.Draw"));
    if(!TestNotNull(TEXT("Water spec"),Spec))return false;
    Spec->Level=3;
    auto* Target=World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Target);Target->SetRootComponent(Box);Target->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(50));Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();
    Target->SetActorLocation(FVector(500,0,55));
    auto* Body=NewObject<UReactiveBodyComponent>(Target);Target->AddInstanceComponent(Body);
    Body->Preset=EReactiveMaterialPreset::Wood;Body->RegisterComponent();Target->DispatchBeginPlay();
    if(!TestTrue(TEXT("Target registered in real simulation"),Body->GetBodyId()!=Reactive::InvalidBody))return false;
    C->WaterReserveKg=.75f;
    TestFalse(TEXT("Upgrade requires its full output from reserve"),C->TrySkill(TEXT("Water.Draw")));
    TestEqual(TEXT("Rejected shortage spends no water"),C->WaterReserveKg,.75f);
    TestEqual(TEXT("Rejected shortage spends no mana"),C->Mana(),100.f);
    C->WaterReserveKg=3;
    TestTrue(TEXT("Rank three water accepts real target"),C->TrySkill(TEXT("Water.Draw")));
    TestEqual(TEXT("Exactly one kilogram deducted"),C->WaterReserveKg,2.f);
    TestEqual(TEXT("Rank three mana deducted"),C->Mana(),82.f);
    auto* ReactiveWorld=World->GetSubsystem<UReactiveWorldSubsystem>();
    World->SetBegunPlay(true); // 隔离世界只推进本次注入，不启动整张生产地图。
    ReactiveWorld->Tick(.05f);
    const auto* State=ReactiveWorld->GetSimulation()->Find(Body->GetBodyId());
    if(!TestNotNull(TEXT("Target simulation state"),State))return false;
    // 木材只容纳 0.05 kg，其余在模拟的溢流统计中；升级不能突破材料承水容量。
    TestTrue(TEXT("Material capacity still limits retained water"),FMath::IsNearlyEqual(State->WaterKg,.05,1.e-6));
    TestTrue(TEXT("Reserve plus retained plus rejected water conserves the input"),
        FMath::IsNearlyEqual(double(C->WaterReserveKg)+State->WaterKg+ReactiveWorld->GetSimulation()->GetStats().RejectedWaterKg,3.,1.e-6));
    C->ResetCombat();
    // 模拟可命中但无注入权限的目标：Commit 后拒绝必须退回法力且不丢水。
    Body->bOwnerOnlyStimuli=true;
    C->TrySkill(TEXT("Water.Draw"));
    TestEqual(TEXT("Rejected injection refunds mana"),C->Mana(),82.f);
    TestEqual(TEXT("Rejected injection keeps reserve"),C->WaterReserveKg,2.f);
    TestEqual(TEXT("Rejected injection starts no cooldown"),C->CastLockUntil,0.f);
    return true;
}
#endif
