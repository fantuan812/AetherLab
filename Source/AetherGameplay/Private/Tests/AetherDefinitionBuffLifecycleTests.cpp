#include "Misc/AutomationTest.h"
#include "Combat/AetherCombat.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Framework/AetherProgression.h"
#include "Skills/AetherSkillCooldownState.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Effects/AetherBuffRuntime.h"
#include "Inventory/AetherResourceGate.h"
#include "AetherEquipmentComponent.h"
#include "AIController.h"
#include "Engine/World.h"
#include "TimerManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FDefinitionBuffFixture
{
    UWorld* World=nullptr;
    AAetherCharacter *Subject=nullptr,*Patient=nullptr,*Other=nullptr,*Queued=nullptr,*Ended=nullptr;
    AAetherFrontierCharacter* Native=nullptr;
    AAetherPlayerState* State=nullptr;
    AAIController* Controller=nullptr;
    FGuid OldLife,NewLife,Execution,Reservation;
    float ManaBefore=0,HealthBefore=0;
    bool Reentered=false,AppliedReplacement=false;
    int32 Debits=0,Refunds=0;
    FDelegateHandle HealthHook,ManaHook,PhaseHook;
    ~FDefinitionBuffFixture(){if(World){World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);World->RemoveFromRoot();}}
};
class FDefinitionBuffStep final:public IAutomationLatentCommand
{
    TFunction<void()> Work;
public:
    explicit FDefinitionBuffStep(TFunction<void()> In):Work(MoveTemp(In)){}
    virtual bool Update() override {Work();return true;}
};
class FDefinitionBuffAdvance final:public IAutomationLatentCommand
{
    TSharedPtr<FDefinitionBuffFixture> Fixture;
    FAutomationTestBase* Test;
    double Seconds,Until=-1;
    int32 Frames=0;
public:
    FDefinitionBuffAdvance(TSharedPtr<FDefinitionBuffFixture> In,FAutomationTestBase* Owner,double Duration):Fixture(MoveTemp(In)),Test(Owner),Seconds(Duration){}
    virtual bool Update() override
    {
        auto* W=Fixture->World;if(Until<0)Until=W->GetTimeSeconds()+Seconds;
        if(W->GetTimeSeconds()>=Until)return true;
        if(++Frames>600){Test->AddError(TEXT("Isolated world/timer failed to advance within bounded frames"));return true;}
        // 每个真实自动化帧最多推进一次，不能在同帧反复Tick TimerManager造成假前摇覆盖。
        if(W->GetTimerManager().HasBeenTickedThisFrame())return false;
        W->Tick(LEVELTICK_TimeOnly,.1f);W->GetTimerManager().Tick(.1f);return false;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherDefinitionBuffLifecycleTest,"Aether.AI.Buffs.DefinitionLifeAndPeriodicHealing",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherDefinitionBuffLifecycleTest::RunTest(const FString&)
{
    auto F=MakeShared<FDefinitionBuffFixture>();F->World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated world"),F->World))return false;F->World->AddToRoot();
    const auto Spawn=[&](FVector Position)
    {
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* C=F->World->SpawnActor<AAetherCharacter>(Position,FRotator::ZeroRotator,P);
        if(!C)return C;
        C->bUseBasicAssets=true;C->SkillAuthority=EAetherSkillAuthority::Definition;C->SkillLoadoutId=TEXT("Companion.Healer");
        C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());C->AbilitySystem->InitAbilityActorInfo(C,C);C->SetVitals(50,100,100);C->GrantSpells();return C;
    };
    F->Subject=Spawn(FVector(0,0,88));F->Patient=Spawn(FVector(300,0,88));F->Other=Spawn(FVector(5000,0,88));
    F->Queued=Spawn(FVector(6000,0,88));F->Ended=Spawn(FVector(7000,0,88));F->Controller=F->World->SpawnActor<AAIController>();
    F->Native=F->World->SpawnActor<AAetherFrontierCharacter>(FVector(10000,0,88),FRotator::ZeroRotator);
    F->State=F->World->SpawnActor<AAetherPlayerState>();
    if(!F->Subject||!F->Patient||!F->Other||!F->Queued||!F->Ended||!F->Controller||!F->Native||!F->State)return false;
    F->Controller->Possess(F->Subject);F->Controller->SetControlRotation(FRotator::ZeroRotator);
    F->State->Profile.CharacterId=TEXT("DefinitionBuffNativeFixture");F->Native->SetPlayerState(F->State);F->Native->BindPersistentAbilities();
    F->Native->AbilitySystem->AddAttributeSetSubobject(F->Native->Attributes.Get());
    FAetherProfileStateV10 Profile;Profile.CharacterId=F->State->Profile.CharacterId;FString Why;
    if(!TestTrue(*Why,F->State->PublishNativeProfile(Profile,Why)&&F->State->PublishNativeSkills(Profile,{},Why)&&
        F->Native->ResourceGate->BeginFullRespawn(Profile.CharacterId)&&F->Native->ResourceGate->FinishRecovery()))return false;
    F->World->SetBegunPlay(true);
    const auto Step=[&](TFunction<void()> Work){FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FDefinitionBuffStep>(MoveTemp(Work)));};
    const auto Advance=[&](double Seconds){FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FDefinitionBuffAdvance>(F,this,Seconds));};
    Step([F,this]{
        auto* C=F->Subject;FGuid Life;FString Why;
        TestTrue(TEXT("Definition has a real ASC life without a fake profile/receiver"),AetherSkillLives::Resolve(*C,Life)&&Life.IsValid()&&!C->GetPlayerState<AAetherPlayerState>()&&!C->ResourceGate->GetReceiver());
        TestTrue(TEXT("Actual healer data grants both formal support skills"),C->SkillUnlocked(TEXT("Body.Aid"))&&C->SkillUnlocked(TEXT("Body.Mend")));
        for(const auto* Id:{TEXT("Sample.Haste"),TEXT("Inn.WaterTraining"),TEXT("Sample.Silence"),TEXT("Sample.Poison")})
        {
            TestFalse(TEXT("Unsupported Definition operation is rejected"),C->BuffRuntime->CanApply(Id,&Why));TestFalse(TEXT("Rejected operation has a diagnostic"),Why.IsEmpty());
            TestFalse(TEXT("Apply cannot bypass Definition capability filtering"),C->BuffRuntime->Apply(Id,TEXT("Fixture.Unsupported"),Why));
        }
        TestTrue(TEXT("Definition accepts existing periodic Health effect"),C->BuffRuntime->Apply(TEXT("Sample.Regeneration"),TEXT("Fixture.Period"),Why));
        TestEqual(TEXT("Applying regeneration is not an instant heal"),C->Health(),50.f);
        TestTrue(TEXT("Effect state uses the sole ASC life"),C->BuffRuntime->GetState().LifeId==Life);
    });
    Advance(8.05);
    Step([F,this]{F->Subject->BuffRuntime->FlushDue();TestEqual(TEXT("Four real due events heal sixteen"),F->Subject->Health(),66.f);});
    Advance(2.05);
    Step([F,this]{
        auto* C=F->Subject;C->BuffRuntime->FlushDue();TestEqual(TEXT("Expiry produces no fifth heal"),C->Health(),66.f);
        TestTrue(TEXT("Effect really expired"),C->BuffRuntime->GetState().Instances.IsEmpty());
        FGuid Before,After;AetherSkillLives::Resolve(*C,Before);F->Controller->UnPossess();
        TestFalse(TEXT("Control gap is not a valid skill owner"),AetherSkillLives::Resolve(*C,After));
        F->Controller->Possess(C);TestTrue(TEXT("Same-body controller handoff preserves life"),AetherSkillLives::Resolve(*C,After)&&Before==After);
        AetherSkillLives::Resolve(*C,F->OldLife);C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),0);
        TestFalse(TEXT("Direct authoritative death invalidates life synchronously"),AetherSkillLives::Resolve(*C,After));
        C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),60);
        TestTrue(TEXT("Same-frame direct revival has a new life"),AetherSkillLives::Resolve(*C,F->NewLife)&&F->NewLife!=F->OldLife);
        F->Controller->UnPossess();C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),0);C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),60);
        F->Controller->Possess(C);TestTrue(TEXT("Death inside a control gap is not mistaken for an ordinary handoff"),AetherSkillLives::Resolve(*C,After)&&After!=F->NewLife);
        C->SetVitals(60,100,100);F->ManaBefore=C->Mana();F->Debits=0;
        F->ManaHook=C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).AddLambda([F](const FOnAttributeChangeData& Change){if(Change.NewValue<Change.OldValue)++F->Debits;});
        TestTrue(TEXT("Real canonical self skill enters its actual WindupTimer"),C->TrySkill(TEXT("Body.Mend")));
        F->Execution=C->CastExecutionId;AetherSkillLives::Resolve(*C,F->OldLife);
        TestTrue(TEXT("Windup execution exists before direct death/revival"),F->Execution.IsValid());
        C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),0);C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),60);
        TestTrue(TEXT("Direct ASC edge changes life without calling CancelActions"),C->CastExecutionId==F->Execution&&AetherSkillLives::Resolve(*C,F->NewLife)&&F->NewLife!=F->OldLife);
    });
    Advance(1.7);
    Step([F,this]{
        auto* C=F->Subject;C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).Remove(F->ManaHook);
        TestTrue(TEXT("Old real windup exits before charging new-life mana"),F->Debits==0&&C->Mana()==F->ManaBefore&&!C->CastExecutionId.IsValid());
        TestEqual(TEXT("Old windup cannot create a new-life effect"),C->BuffRuntime->GetState().Instances.Num(),0);
        TestTrue(TEXT("New life can use the actual self skill"),C->TrySkill(TEXT("Body.Mend")));
    });
    Advance(1.7);
    Step([F,this]{
        auto* C=F->Subject;TestEqual(TEXT("Formal cast pays authored mana"),C->Mana(),85.f);
        TestTrue(TEXT("Formal cast applied existing regeneration and real ASC cooldown"),C->BuffRuntime->GetState().Instances.Num()==1&&C->SkillCooldownRemaining(TEXT("Body.Mend"))>0);
        const float Before=C->SkillCooldownRemaining(TEXT("Body.Mend"));FGuid Life;AetherSkillLives::Resolve(*C,Life);
        F->Controller->UnPossess();F->Controller->Possess(C);FGuid After;AetherSkillLives::Resolve(*C,After);
        TestTrue(TEXT("Control handoff does not clear buff life or authoritative cooldown"),Life==After&&C->SkillCooldownRemaining(TEXT("Body.Mend"))==Before);
        FString Why;C->BuffRuntime->Dispel(TEXT("Positive"),Why);C->ResetCombat();
        // 真实近战OnAvatarSet取消回调将绑定往返原体，不能只测最终Avatar不同。
        auto* E=C->Equipment.Get();auto* Catalog=NewObject<UAetherEquipmentCatalog>(C);auto* Item=NewObject<UAetherEquipmentDefinition>(Catalog);
        Item->ItemId=TEXT("Fixture.AvatarLife");Item->Mesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
        FAetherAttackDefinition Attack;Attack.Id=TEXT("Light");Attack.StaminaCost=0;Attack.WindupSeconds=1;Item->Attacks={Attack};Catalog->Items={Item};E->Catalog=Catalog;
        FAetherEquippedSlot Slot;Slot.Slot=TEXT("MainHand");Slot.ItemId=Item->ItemId;E->RestoreLoadout({Slot});
        E->CanAct.BindUObject(C,&AAetherCharacter::Ready);TestTrue(TEXT("Real melee is active for Avatar cancellation"),C->RequestMelee(TEXT("Light")));
        AetherSkillLives::Resolve(*C,F->OldLife);F->Reentered=false;
        F->PhaseHook=E->OnAttackPhaseChanged.AddLambda([F](uint32,EAetherAttackPhase Phase){if(Phase==EAetherAttackPhase::Cancelled&&!F->Reentered){F->Reentered=true;F->Subject->AbilitySystem->InitAbilityActorInfo(F->Subject,F->Subject);}});
        C->AbilitySystem->InitAbilityActorInfo(C,F->Other);E->OnAttackPhaseChanged.Remove(F->PhaseHook);
        TestTrue(TEXT("Actual Avatar round trip still creates a new life"),F->Reentered&&C->AbilitySystem->GetAvatarActor()==C&&AetherSkillLives::Resolve(*C,F->NewLife)&&F->NewLife!=F->OldLife);
        C->BuffRuntime->FlushDue();C->SetVitals(60,77,33);F->Reentered=false;
        TestTrue(TEXT("Prepare an actual healing event"),C->BuffRuntime->Apply(TEXT("Sample.Regeneration"),TEXT("Fixture.ReentrantHeal"),Why));
        F->HealthHook=C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).AddLambda([F](const FOnAttributeChangeData& Change){
            if(Change.NewValue<=Change.OldValue||F->Reentered)return;F->Reentered=true;auto* C=F->Subject;
            C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),0);C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),40);
            C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetManaAttribute(),21);C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetStaminaAttribute(),19);
        });
    });
    Advance(2.1);
    Step([F,this]{
        auto* C=F->Subject;C->BuffRuntime->FlushDue();C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).Remove(F->HealthHook);
        TestTrue(TEXT("Health callback really entered a replacement life"),F->Reentered&&C->Health()==40);
        TestTrue(TEXT("Old heal cannot overwrite new-life mana/stamina"),C->Mana()==21&&C->Stamina()==19);
        C->BuffRuntime->FlushDue();TestTrue(TEXT("Old life effect state is discarded"),C->BuffRuntime->GetState().Instances.IsEmpty());
        // 使用正式屏障注入延期队列；不伪造PlayerState或数据库完成。
        auto* Q=F->Queued;FString Why;TestTrue(TEXT("Establish real queue barrier fixture"),Q->ResourceGate->BeginFullRespawn(TEXT("DefinitionQueueFixture"))&&Q->ResourceGate->FinishRecovery());
        Q->SetVitals(50,100,100);TestTrue(TEXT("Apply queued fixture effect"),Q->BuffRuntime->Apply(TEXT("Sample.Regeneration"),TEXT("Fixture.Queue"),Why));
        FAetherResourceStateV10 Before;F->Reservation=FGuid::NewGuid();TestTrue(TEXT("Reserve actual gate"),Q->ResourceGate->Reserve(F->Reservation,Before));
    });
    Advance(2.1);
    Step([F,this]{
        auto* Q=F->Queued;Q->BuffRuntime->FlushDue();TestTrue(TEXT("Real due work entered gate"),Q->ResourceGate->Inspect().DeferredCount>0);
        AetherSkillLives::Resolve(*Q,F->OldLife);Q->AbilitySystem->ClearActorInfo();
        Q->ResourceGate->CancelKnownUncommitted(F->Reservation);Q->ResourceGate->TickComponent(.01f,LEVELTICK_All,nullptr);
        Q->AbilitySystem->InitAbilityActorInfo(Q,Q);FGuid Life;AetherSkillLives::Resolve(*Q,Life);
        TestTrue(TEXT("Queue control gap preserves resource life"),Life==F->OldLife);
        Q->BuffRuntime->FlushDue();TestTrue(TEXT("Dequeued unavailable callback retired its ticket and due work can retry"),Q->Health()==54&&!Q->BuffRuntime->HasDue());
        FAetherResourceStateV10 Before;F->Reservation=FGuid::NewGuid();Q->ResourceGate->Reserve(F->Reservation,Before);F->AppliedReplacement=false;
        Q->ResourceGate->Defer([F]{auto* Q=F->Queued;
            Q->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),0);Q->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),60);
            FString Why;F->AppliedReplacement=Q->BuffRuntime->Apply(TEXT("Sample.Regeneration"),TEXT("Fixture.Queue"),Why);
        });
    });
    Advance(2.1);
    Step([F,this]{
        auto* Q=F->Queued;Q->BuffRuntime->FlushDue();Q->BuffRuntime->RemoveSource(TEXT("Fixture.Queue"));
        Q->ResourceGate->CancelKnownUncommitted(F->Reservation);Q->ResourceGate->TickComponent(.01f,LEVELTICK_All,nullptr);
        TestTrue(TEXT("Old queued tick/removal cannot heal or erase the new life's effect"),F->AppliedReplacement&&Q->Health()==60&&Q->BuffRuntime->GetState().Instances.Num()==1&&!Q->BuffRuntime->HasDue());
        auto* N=F->Native;FString Why;FGuid Life;
        TestTrue(TEXT("Native Profile still resolves its own Receiver life"),AetherSkillLives::Resolve(*N,Life)&&Life==N->ResourceGate->GetReceiver()->State().LifeId);
        N->SetVitals(60,100,100);TestTrue(TEXT("Native attribute buffs retain existing projection"),N->BuffRuntime->Apply(TEXT("Sample.Vitality"),TEXT("Fixture.Native"),Why)&&N->MaxHealth==120&&N->Health()==60);
        auto* C=F->Other;C->SkillAuthority=EAetherSkillAuthority::Profile;
        TestFalse(TEXT("Missing Profile owner does not fall through to Definition ASC"),AetherSkillLives::Resolve(*C,Life));
        TestFalse(TEXT("Missing Profile cannot consume Definition periodic capability"),C->BuffRuntime->CanApply(TEXT("Sample.Regeneration"),&Why));C->SkillAuthority=EAetherSkillAuthority::Definition;
        F->Ended->DispatchBeginPlay();F->Ended->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
        TestTrue(TEXT("Ended actor stays alive without a usable life"),IsValid(F->Ended)&&F->Ended->Alive()&&!AetherSkillLives::Resolve(*F->Ended,Life));
    });
    for(int32 Mode=0;Mode<2;++Mode)
    {
        Step([F,this]{
            FString Why;F->Patient->BuffRuntime->Dispel(TEXT("Positive"),Why);F->Patient->SetVitals(50,100,100);
            F->Subject->ResetCombat();F->Subject->SetVitals(80,100,100);F->Controller->SetControlRotation(FRotator::ZeroRotator);
            TestTrue(TEXT("Prepare target due healing before a real friendly windup"),F->Patient->BuffRuntime->Apply(TEXT("Sample.Regeneration"),TEXT("Fixture.TargetDue"),Why));
        });
        Advance(1.6);
        Step([F,this,Mode]{
            F->Reentered=false;F->Debits=F->Refunds=0;
            F->HealthHook=F->Patient->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).AddLambda([F,Mode](const FOnAttributeChangeData& Change){
                if(Change.NewValue<=Change.OldValue||F->Reentered)return;F->Reentered=true;auto* C=F->Subject;
                if(Mode==0)C->CancelActions();
                else {C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),0);C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),80);
                    C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetManaAttribute(),31);C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetStaminaAttribute(),29);}
            });
            F->ManaHook=F->Subject->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).AddLambda([F](const FOnAttributeChangeData& Change){
                if(Change.NewValue<Change.OldValue)++F->Debits;else if(Change.NewValue>Change.OldValue)++F->Refunds;
            });
            TestTrue(TEXT("Real Body.Aid enters existing target and windup contract"),F->Subject->TrySkill(TEXT("Body.Aid"))&&F->Subject->CastExecutionId.IsValid());
        });
        Advance(.8);
        Step([F,this,Mode]{
            auto* C=F->Subject;F->Patient->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).Remove(F->HealthHook);
            C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).Remove(F->ManaHook);
            TestTrue(TEXT("Friendly precheck really processed target healing and reentered source"),F->Reentered&&F->Patient->Health()==54);
            TestTrue(TEXT("Canceled or replaced source cannot apply a second regeneration instance"),F->Patient->BuffRuntime->GetState().Instances.Num()==1);
            TestTrue(TEXT("Rejected source has no skill cooldown or stale recovery"),C->SkillCooldownRemaining(TEXT("Body.Aid"))==0&&C->CastLockUntil==0&&!C->CastExecutionId.IsValid());
            if(Mode==0)TestTrue(TEXT("Same-life cancellation refunds the actual paid mana once"),F->Debits==1&&F->Refunds==1&&C->Mana()==100);
            else TestTrue(TEXT("Old-life rejection never refunds or overwrites replacement resources"),C->Mana()==31&&C->Stamina()==29);
        });
    }
    Step([F,this]{
        auto* N=F->Native;FString Why;N->BuffRuntime->Dispel(TEXT("Positive"),Why);N->SetVitals(50,100,100);
        TestTrue(TEXT("Real Profile regeneration still applies"),N->BuffRuntime->Apply(TEXT("Sample.Regeneration"),TEXT("Fixture.NativeQueue"),Why));
        FAetherResourceStateV10 Before;F->Reservation=FGuid::NewGuid();TestTrue(TEXT("Reserve native Profile barrier"),N->ResourceGate->Reserve(F->Reservation,Before));
    });
    Advance(2.1);
    Step([F,this]{
        auto* N=F->Native;N->BuffRuntime->FlushDue();TestTrue(TEXT("Profile due work was really queued"),N->ResourceGate->Inspect().DeferredCount>0);
        AetherSkillLives::Resolve(*N,F->OldLife);N->AbilitySystem->ClearActorInfo();
        N->ResourceGate->CancelKnownUncommitted(F->Reservation);N->ResourceGate->TickComponent(.01f,LEVELTICK_All,nullptr);
        N->AbilitySystem->InitAbilityActorInfo(F->State,N);FGuid Life;AetherSkillLives::Resolve(*N,Life);N->BuffRuntime->FlushDue();
        TestTrue(TEXT("Same Profile life retries retired unavailable work without permanent action lock"),Life==F->OldLife&&N->Health()==54&&!N->BuffRuntime->HasDue());
    });
    Step([F,this]{
        auto* C=F->Other;FString Why;C->BuffRuntime->Dispel(TEXT("Positive"),Why);C->SetVitals(C->MaxHealth-1,77,33);
        TestTrue(TEXT("Prepare Health-only upper-bound event"),C->BuffRuntime->Apply(TEXT("Sample.Regeneration"),TEXT("Fixture.Clamp"),Why));
    });
    Advance(2.1);
    Step([F,this]{
        auto* C=F->Other;C->BuffRuntime->FlushDue();
        TestTrue(TEXT("Health-only tick still uses AttributeSet MaxHealth clamp"),C->Health()==C->MaxHealth&&C->Mana()==77&&C->Stamina()==33);
    });
    for(int32 Mode=0;Mode<2;++Mode)
    {
        Step([F,this]{
            auto* C=F->Other;FString Why;C->BuffRuntime->Dispel(TEXT("Positive"),Why);C->SetVitals(50,100,100);
            TestTrue(TEXT("Prepare due work inside a public mutation entry"),C->BuffRuntime->Apply(TEXT("Sample.Regeneration"),TEXT("Fixture.EntryDue"),Why));
        });
        Advance(2.1);
        Step([F,this,Mode]{
            auto* C=F->Other;F->Reentered=F->AppliedReplacement=false;AetherSkillLives::Resolve(*C,F->OldLife);
            FGuid ReplacementId;uint64 ReplacementRevision=0;
            const auto Hook=C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).AddLambda([F,&ReplacementId,&ReplacementRevision](const FOnAttributeChangeData& Change){
                if(Change.NewValue<=Change.OldValue||F->Reentered)return;F->Reentered=true;auto* C=F->Other;
                C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),0);C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),60);
                FString Why;F->AppliedReplacement=C->BuffRuntime->Apply(TEXT("Sample.Regeneration"),TEXT("Fixture.Replacement"),Why);
                const auto& State=C->BuffRuntime->GetState();ReplacementRevision=State.Revision;
                if(State.Instances.Num()==1)ReplacementId=State.Instances[0].InstanceId;
            });
            FString Why;const bool Applied=Mode==0?C->BuffRuntime->Apply(TEXT("Sample.Regeneration"),TEXT("Fixture.OldRequest"),Why):C->BuffRuntime->Dispel(TEXT("Positive"),Why);
            C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).Remove(Hook);
            FGuid Current;AetherSkillLives::Resolve(*C,Current);const auto& State=C->BuffRuntime->GetState();
            TestTrue(TEXT("Mutation entry really reentered a different life with its own effect"),F->Reentered&&F->AppliedReplacement&&Current!=F->OldLife&&ReplacementId.IsValid());
            TestTrue(TEXT("Original Apply/Dispel rejects after its due work changed life"),!Applied&&!Why.IsEmpty());
            TestTrue(TEXT("Old mutation cannot refresh or dispel the replacement effect"),State.LifeId==Current&&State.Revision==ReplacementRevision&&State.Instances.Num()==1&&State.Instances[0].InstanceId==ReplacementId&&State.Instances[0].Sources.Contains(TEXT("Fixture.Replacement"))&&!State.Instances[0].Sources.Contains(TEXT("Fixture.OldRequest")));
        });
    }
    Step([F]{auto* W=F->World;F->World=nullptr;W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);W->RemoveFromRoot();});
    return true;
}
#endif
