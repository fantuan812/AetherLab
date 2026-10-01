#include "Misc/AutomationTest.h"
#include "AI/AetherNpcSkillDecision.h"
#include "Combat/AetherCombat.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Skills/AetherSkillDefinitions.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "ReactiveWorldSubsystem.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNpcSkillDecisionTest,"Aether.AI.Skills.DataSelectionAndAuthoritativeCommit",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNpcSkillDecisionTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated AI world"),World))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    const auto Spawn=[&](EAetherFighter Fighter,const FString& Loadout,FVector Location)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Character=World->SpawnActor<AAetherCharacter>(Location,FRotator::ZeroRotator,Params);
        if(!Character)return Character;
        Character->Fighter=Fighter;Character->SkillLoadoutId=Loadout;Character->bUseBasicAssets=true;
        Character->AbilitySystem->AddAttributeSetSubobject(Character->Attributes.Get());
        Character->AbilitySystem->InitAbilityActorInfo(Character,Character);Character->SetVitals(100,100,100);Character->GrantSpells();return Character;
    };
    auto* Npc=Spawn(EAetherFighter::FireCaster,TEXT("FireCaster"),FVector::ZeroVector);
    auto* Target=Spawn(EAetherFighter::Player,TEXT("TrainingElementalist"),FVector(1000,0,0));
    auto* Controller=World->SpawnActor<AAIController>();
    if(!TestNotNull(TEXT("Caster"),Npc)||!TestNotNull(TEXT("Current hostile target"),Target)||!TestNotNull(TEXT("AI controller"),Controller))return false;
    Npc->SetActorEnableCollision(false);Controller->Possess(Npc);Controller->SetControlRotation(FRotator::ZeroRotator);
    Target->DispatchBeginPlay();
    if(!TestTrue(TEXT("Real target participates in reactive simulation"),Target->Reactive->GetBodyId()!=Reactive::InvalidBody))return false;
    auto* Fire=AetherSkillBinding::Find(*Npc->AbilitySystem,TEXT("Fire.Ignite"));
    auto* Storm=AetherSkillBinding::Find(*Npc->AbilitySystem,TEXT("Storm.Strike"));
    if(!TestNotNull(TEXT("Granted fire"),Fire)||!TestNotNull(TEXT("Granted storm"),Storm))return false;
    Fire->Level=3;Fire->InputID=3;Storm->InputID=0;
    const auto* FireEffect=FAetherSkillDefinitionsV10::Get().Effect(TEXT("Fire.Ignite"),3);
    if(!TestNotNull(TEXT("Rank-derived fire effect"),FireEffect))return false;
    auto& Decision=Npc->EnemySkillDecision;
    const auto First=Decision.Choose(*Npc,*Target);
    TestTrue(TEXT("Data selects stable fire identity after input slots are swapped"),First.Kind==EAetherNpcSkillChoice::Ready&&First.SkillId==TEXT("Fire.Ignite"));
    TestEqual(TEXT("Decision range uses current authoritative spec rank"),First.RangeCm,FireEffect->RangeCm);
    TestTrue(TEXT("Choosing alone cannot advance the rotation"),Decision.LastCommittedSkill().IsEmpty());
    Npc->Think(.1f); // 真实生产感知 -> 单一 Think -> 数据决策 -> GAS -> 成功提交反馈。
    TestEqual(TEXT("Think actually pays the rank-three fire cost"),Npc->Mana(),100.f-float(FireEffect->ManaCost));
    TestEqual(TEXT("Only successful ExecuteCast advances rotation"),Decision.LastCommittedSkill(),FString(TEXT("Fire.Ignite")));
    Npc->ResetCombat();
    TestTrue(TEXT("Reset does not remove the actual ASC group cooldown"),Npc->SkillCooldownRemaining(TEXT("Storm.Strike"))>0);
    TestTrue(TEXT("Decision waits for GAS despite cleared recovery"),Decision.Choose(*Npc,*Target).Kind==EAetherNpcSkillChoice::Waiting);
    const auto AdvanceCooldown=[&]()
    {
        const float Remaining=Npc->SkillCooldownRemaining(TEXT("Fire.Ignite"));
        if(!FMath::IsFinite(Remaining)||Remaining>10)return false;
        World->SetBegunPlay(true);World->Tick(LEVELTICK_TimeOnly,FMath::Max(0.f,Remaining)+.1f);Npc->ResetCombat();
        return Npc->SkillCooldownRemaining(TEXT("Fire.Ignite"))<=0;
    };
    if(!TestTrue(TEXT("Actual world clock expires the group deadline"),AdvanceCooldown()))return false;
    auto Next=Decision.Choose(*Npc,*Target);
    TestTrue(TEXT("Next skill comes from the data-defined stable order"),Next.Kind==EAetherNpcSkillChoice::Ready&&Next.SkillId==TEXT("Storm.Strike"));

    // 支付通知改变目标阵营：真实 Commit 已扣费，但效果前必须重新校验 AI 意图并退款。
    Npc->SetVitals(100,100,100);bool ChangedTarget=false;int32 Debits=0,Refunds=0;
    const auto Payment=Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).AddLambda([&](const FOnAttributeChangeData& Change)
    {
        if(Change.NewValue<Change.OldValue){++Debits;if(!ChangedTarget){ChangedTarget=true;Target->Fighter=EAetherFighter::ShieldGuard;}}
        else if(Change.NewValue>Change.OldValue)++Refunds;
    });
    TestTrue(TEXT("GAS accepts the attempt before the target changes"),Decision.TryExecute(*Npc,*Target,Next.SkillId));
    Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).Remove(Payment);
    TestTrue(TEXT("Target really changes synchronously during payment"),ChangedTarget&&Debits==1&&Refunds==1);
    TestEqual(TEXT("Rejected commit refunds its mana"),Npc->Mana(),100.f);
    TestEqual(TEXT("Rejected commit cannot advance rotation"),Decision.LastCommittedSkill(),FString(TEXT("Fire.Ignite")));
    TestEqual(TEXT("Rejected commit cannot start a skill cooldown"),Npc->SkillCooldownRemaining(TEXT("Storm.Strike")),0.f);
    Target->Fighter=EAetherFighter::Player;
    Next=Decision.Choose(*Npc,*Target);
    TestEqual(TEXT("Failed delivery retries the same next identity"),Next.SkillId,FString(TEXT("Storm.Strike")));

    // 同步零前摇在 ValidateCommit 前取消，标记必须来自当时 GAS 的真实执行身份。
    FGuid CanceledExecution;bool CanceledDuringPayment=false;
    const auto CancelPayment=Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).AddLambda([&](const FOnAttributeChangeData& Change)
    {
        if(!CanceledDuringPayment&&Change.NewValue<Change.OldValue)
        {CanceledDuringPayment=true;CanceledExecution=Npc->CastExecutionId;Npc->CancelActions();}
    });
    Decision.TryExecute(*Npc,*Target,Next.SkillId);
    Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).Remove(CancelPayment);
    TestTrue(TEXT("Cancellation observes a real GAS execution before the helper binds its return"),CanceledDuringPayment&&CanceledExecution.IsValid());
    TestEqual(TEXT("Canceled request refunds payment"),Npc->Mana(),100.f);
    TestEqual(TEXT("Canceled request cannot advance rotation"),Decision.LastCommittedSkill(),FString(TEXT("Fire.Ignite")));
    TestEqual(TEXT("Canceled request cannot start cooldown"),Npc->SkillCooldownRemaining(TEXT("Storm.Strike")),0.f);
    FAetherCastExecution Late;Late.SkillId=Next.SkillId;Late.ExecutionId=CanceledExecution;Late.Rank=1;
    TestFalse(TEXT("Late canceled execution cannot use the now-empty intent path"),Decision.ValidateCommit(*Npc,Late));
    FGuid NewExecution;
    const auto ObserveExecution=Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).AddLambda([&](const FOnAttributeChangeData& Change)
    {if(Change.NewValue<Change.OldValue)NewExecution=Npc->CastExecutionId;});
    TestTrue(TEXT("Valid second skill requests the real GAS ability"),Decision.TryExecute(*Npc,*Target,Next.SkillId));
    Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).Remove(ObserveExecution);
    TestTrue(TEXT("New execution does not inherit the previous cancellation"),NewExecution.IsValid()&&NewExecution!=CanceledExecution);
    TestEqual(TEXT("Accepted reactive delivery commits the second identity"),Decision.LastCommittedSkill(),FString(TEXT("Storm.Strike")));
    TestTrue(TEXT("Second effect reaches the existing reactive solver input queue"),World->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation()->HasPendingInputs());

    const float BeforeControlChange=Npc->SkillCooldownRemaining(TEXT("Fire.Ignite"));
    Controller->UnPossess();Controller->Possess(Npc);
    TestTrue(TEXT("Controller changes clear only selection history"),Decision.LastCommittedSkill().IsEmpty());
    TestFalse(TEXT("Controller reset cannot resurrect the canceled execution"),Decision.ValidateCommit(*Npc,Late));
    TestEqual(TEXT("Controller changes cannot erase ASC cooldown"),Npc->SkillCooldownRemaining(TEXT("Fire.Ignite")),BeforeControlChange);
    TestTrue(TEXT("New decision generation still waits for GAS"),Decision.Choose(*Npc,*Target).Kind==EAetherNpcSkillChoice::Waiting);

    const auto FireHandle=AetherSkillBinding::Find(*Npc->AbilitySystem,TEXT("Fire.Ignite"))->Handle;
    Npc->AbilitySystem->ClearAbility(FireHandle);
    TestTrue(TEXT("Missing required spec is unavailable, never melee or another spell fallback"),Decision.Choose(*Npc,*Target).Kind==EAetherNpcSkillChoice::Unavailable);
    FString Reason;if(!TestTrue(TEXT("Restore production definition grants"),Npc->GrantDefinitionSkills(Reason)))return false;
    Npc->SkillLoadoutId=TEXT("Missing.Loadout");
    TestTrue(TEXT("Missing loadout fails closed"),Decision.Choose(*Npc,*Target).Kind==EAetherNpcSkillChoice::Unavailable);
    Npc->SkillLoadoutId=TEXT("MeleeOnly");
    TestTrue(TEXT("Explicit empty strategy is a declared melee configuration"),Decision.Choose(*Npc,*Target).Kind==EAetherNpcSkillChoice::NoOffensiveSkills);
    Npc->SkillLoadoutId=TEXT("FireCaster");
    if(!TestTrue(TEXT("Subsequent actual clock expiry"),AdvanceCooldown()))return false;
    Fire=AetherSkillBinding::Find(*Npc->AbilitySystem,TEXT("Fire.Ignite"));Fire->Level=3;
    Target->SetActorLocation(FVector(FireEffect->RangeCm+500,0,0));
    const auto Distant=Decision.Choose(*Npc,*Target);
    TestTrue(TEXT("Out-of-range target requests approach using current effect range"),Distant.Kind==EAetherNpcSkillChoice::Approach&&Distant.RangeCm==FireEffect->RangeCm);
    Target->SetActorLocation(FVector(1000,0,0));
    auto* Blocker=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Blocker);
    Blocker->SetRootComponent(Box);Blocker->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(50));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();
    Blocker->SetActorLocation(FVector(500,0,55));
    const float BeforeBlocked=Npc->Mana();
    TestTrue(TEXT("Occluded current target is approached instead of fired through"),Decision.Choose(*Npc,*Target).Kind==EAetherNpcSkillChoice::Approach);
    TestFalse(TEXT("Commit entry also rejects a different blocking actor"),Decision.TryExecute(*Npc,*Target,TEXT("Fire.Ignite")));
    TestEqual(TEXT("Blocked attempt spends no mana"),Npc->Mana(),BeforeBlocked);Blocker->Destroy();
    Target->SetVitals(0,100,100);
    TestTrue(TEXT("Dead target cannot remain an actionable intent"),Decision.Choose(*Npc,*Target).Kind==EAetherNpcSkillChoice::Unavailable);
    Target->SetVitals(100,100,100);Npc->SkillAuthority=EAetherSkillAuthority::Profile;
    TestTrue(TEXT("Profile authority never borrows the NPC decision driver"),Decision.Choose(*Npc,*Target).Kind==EAetherNpcSkillChoice::Unavailable);
    return true;
}
#endif
