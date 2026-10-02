#include "Misc/AutomationTest.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Characters/AetherCompanionComponent.h"
#include "AI/AetherCompanionSupportDefinitions.h"
#include "Skills/AetherSkillCooldownState.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Effects/AetherBuffRuntime.h"
#include "Inventory/AetherResourceGate.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "AIController.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FCompanionSupportFixture
{
    UWorld* World=nullptr;
    AAetherFrontierCharacter *Owner=nullptr,*Healer=nullptr,*Guard=nullptr,*Probe=nullptr,*Patient=nullptr,*Stranger=nullptr;
    AAetherPlayerState* PlayerState=nullptr;
    AAIController *Controller=nullptr,*ProbeController=nullptr;
    bool Reentered=false,ReplacementAccepted=false,EndReentered=false,RestartedControl=false;
    FGuid OldExecution,NewExecution;
    int32 Debits=0,Refunds=0;
    FDelegateHandle ManaHook,HealthHook,EndHook;
    ~FCompanionSupportFixture(){if(World){World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);World->RemoveFromRoot();}}
};
class FCompanionSupportStep final:public IAutomationLatentCommand
{
    TFunction<void()> Work;
public:
    explicit FCompanionSupportStep(TFunction<void()> In):Work(MoveTemp(In)){}
    virtual bool Update() override {Work();return true;}
};
class FCompanionSupportAdvance final:public IAutomationLatentCommand
{
    TSharedPtr<FCompanionSupportFixture> Fixture;
    FAutomationTestBase* Test;
    double Seconds,Until=-1;
    int32 Frames=0;
public:
    FCompanionSupportAdvance(TSharedPtr<FCompanionSupportFixture> F,FAutomationTestBase* T,double S):Fixture(MoveTemp(F)),Test(T),Seconds(S){}
    virtual bool Update() override
    {
        auto* W=Fixture->World;if(Until<0)Until=W->GetTimeSeconds()+Seconds;
        if(W->GetTimeSeconds()>=Until)return true;
        if(++Frames>600){Test->AddError(TEXT("Support fixture world/timers did not advance"));return true;}
        if(W->GetTimerManager().HasBeenTickedThisFrame())return false;
        W->Tick(LEVELTICK_TimeOnly,.1f);W->GetTimerManager().Tick(.1f);return false;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherCompanionHealTest,"Aether.V10.Party.HealingMembershipAndResourceConservation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherCompanionHealTest::RunTest(const FString&)
{
    TestTrue(TEXT("Registry zero and exact maximum remain supported"),UAetherNearbyRegistry::IsSupportedRadius(0)&&UAetherNearbyRegistry::IsSupportedRadius(1000));
    TestFalse(TEXT("Registry rejects over-limit radius"),UAetherNearbyRegistry::IsSupportedRadius(1000.01));
    TestFalse(TEXT("Registry rejects negative radius"),UAetherNearbyRegistry::IsSupportedRadius(-1));
    TestFalse(TEXT("Registry rejects infinity"),UAetherNearbyRegistry::IsSupportedRadius(std::numeric_limits<double>::infinity()));
    TestFalse(TEXT("Registry rejects NaN"),UAetherNearbyRegistry::IsSupportedRadius(std::numeric_limits<double>::quiet_NaN()));
    const auto* Policy=FAetherCompanionSupportDefinitions::Get().ForLoadout(TEXT("Companion.Healer"));
    if(!TestNotNull(TEXT("Actual required support profile"),Policy))return false;
    auto F=MakeShared<FCompanionSupportFixture>();F->World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated support world"),F->World))return false;F->World->AddToRoot();
    const auto Spawn=[&](FVector Position,const FString& Loadout){
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* C=F->World->SpawnActor<AAetherFrontierCharacter>(Position,FRotator::ZeroRotator,P);if(!C)return C;
        C->bUseBasicAssets=true;C->SkillAuthority=EAetherSkillAuthority::Definition;C->SkillLoadoutId=Loadout;
        C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());C->AbilitySystem->InitAbilityActorInfo(C,C);return C;
    };
    F->Owner=Spawn(FVector(300,0,88),TEXT("Companion.Guard"));F->Healer=Spawn(FVector(0,0,88),TEXT("Companion.Healer"));
    F->Guard=Spawn(FVector(300,220,88),TEXT("Companion.Guard"));F->Probe=Spawn(FVector(10000,0,88),TEXT("Companion.Healer"));
    F->Patient=Spawn(FVector(10300,0,88),TEXT("Companion.Guard"));F->Stranger=Spawn(FVector(10300,300,88),TEXT("Companion.Guard"));
    F->Controller=F->World->SpawnActor<AAIController>();F->ProbeController=F->World->SpawnActor<AAIController>();F->PlayerState=F->World->SpawnActor<AAetherPlayerState>();
    if(!F->Owner||!F->Healer||!F->Guard||!F->Probe||!F->Patient||!F->Stranger||!F->Controller||!F->ProbeController||!F->PlayerState)return false;
    F->Owner->SkillAuthority=EAetherSkillAuthority::Profile;F->PlayerState->Profile.CharacterId=TEXT("CompanionSupportFixture");
    F->Owner->SetPlayerState(F->PlayerState);F->Owner->BindPersistentAbilities();F->Owner->AbilitySystem->AddAttributeSetSubobject(F->Owner->Attributes.Get());
    FAetherProfileStateV10 Native;Native.CharacterId=F->PlayerState->Profile.CharacterId;FString Why;
    if(!TestTrue(TEXT("Real Profile owner is fully published"),F->PlayerState->PublishNativeProfile(Native,Why)&&F->PlayerState->PublishNativeSkills(Native,{},Why)&&
        F->Owner->ResourceGate->BeginFullRespawn(Native.CharacterId)&&F->Owner->ResourceGate->FinishRecovery()))return false;
    F->Healer->CompanionOwner=F->Owner;F->Healer->CompanionId=TEXT("Muhe");F->Healer->bHealer=true;
    F->Probe->CompanionOwner=F->Owner;F->Probe->CompanionId=TEXT("FixtureHealer");F->Probe->bHealer=true;
    F->Guard->CompanionOwner=F->Owner;F->Patient->CompanionOwner=F->Owner;
    F->World->SetBegunPlay(true);
    for(auto* C:{F->Owner,F->Healer,F->Guard,F->Probe,F->Patient,F->Stranger}){C->DispatchBeginPlay();C->SetActorTickEnabled(false);C->SetVitals(100,100,100);}
    F->Controller->Possess(F->Healer);F->ProbeController->Possess(F->Probe);
    if(!TestTrue(TEXT("Real AI companion control starts"),F->Healer->CompanionDecision->BeginCompanionControl()&&F->Probe->CompanionDecision->BeginCompanionControl()))return false;
    const auto Step=[&](TFunction<void()> Work){FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCompanionSupportStep>(MoveTemp(Work)));};
    const auto Advance=[&](double Seconds){FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCompanionSupportAdvance>(F,this,Seconds));};
    Step([F,this]{
        auto* C=F->Healer;F->Guard->SetVitals(40,100,100);
        C->CompanionDecision->TickComponent(.01f,LEVELTICK_All,nullptr);
        TestTrue(TEXT("Actual decision selects wounded same-owner guard and real GAS execution"),C->CastExecutionId.IsValid()&&C->CompanionDecision->SupportIntent&&C->CompanionDecision->SupportIntent->Patient.Get()==F->Guard&&C->CompanionDecision->SupportIntent->Execution==C->CastExecutionId);
        F->Controller->UpdateControlRotation(.05f,true);FHitResult Hit;FVector Origin,Direction;
        TestTrue(TEXT("Off-axis patient remains in actual skill aim after controller tick"),C->FindSkillTarget(TEXT("Body.Aid"),1,Hit,Origin,Direction)&&Hit.GetActor()==F->Guard);
        TestEqual(TEXT("No instant twenty-point bypass"),F->Guard->Health(),40.f);TestEqual(TEXT("Windup has not paid early"),C->Mana(),100.f);
    });
    Advance(.8);
    Step([F,this]{
        TestTrue(TEXT("Real aid commits authored cost, effect and cooldown"),F->Healer->Mana()==85&&F->Guard->BuffRuntime->GetState().Instances.Num()==1&&F->Healer->SkillCooldownRemaining(TEXT("Body.Aid"))>0);
        TestEqual(TEXT("Formal aid remains periodic after windup"),F->Guard->Health(),40.f);
    });
    Advance(8.1);
    Step([F,this]{F->Guard->BuffRuntime->FlushDue();TestEqual(TEXT("Existing regeneration heals four ticks, sixteen total"),F->Guard->Health(),56.f);});
    Advance(2.1);
    Step([F,this,Policy]{
        F->Guard->BuffRuntime->FlushDue();TestTrue(TEXT("Expiry is not a fifth tick"),F->Guard->Health()==56&&F->Guard->BuffRuntime->GetState().Instances.IsEmpty());
        F->Healer->CompanionDecision->CancelOwnedSupport();
        TestNull(TEXT("Finished support releases its own aim focus"),F->Controller->GetFocusActorForPriority(EAIFocusPriority::Gameplay));
        TestFalse(TEXT("Decision cannot bypass formal fifteen-second skill cooldown"),F->Healer->CompanionDecision->TryIssueSupport(*F->Healer,*F->Guard,*Policy,EAetherCompanionSupportPurpose::FriendlyHealing));
        TestEqual(TEXT("Cooldown rejection costs no mana"),F->Healer->Mana(),85.f);
        F->Healer->SetVitals(40,100,100);
        TestTrue(TEXT("Distinct formal self skill starts"),F->Healer->CompanionDecision->TryIssueSupport(*F->Healer,*F->Healer,*Policy,EAetherCompanionSupportPurpose::SelfHealing));
    });
    Advance(1.7);
    Step([F,this]{TestTrue(TEXT("Mend uses actual self target and mana cost"),F->Healer->Mana()==85&&F->Healer->BuffRuntime->GetState().Instances.Num()==1&&F->Healer->SkillCooldownRemaining(TEXT("Body.Mend"))>0);});
    Advance(10.2);
    Step([F,this,Policy]{
        F->Healer->BuffRuntime->FlushDue();TestEqual(TEXT("Self healing uses the same sixteen-point periodic content"),F->Healer->Health(),56.f);
        F->Healer->CompanionDecision->CancelOwnedSupport();F->Owner->SetVitals(40,100,100);F->Healer->SetVitals(100,100,100);
        TestTrue(TEXT("Definition healer can target actual Profile owner"),F->Healer->CompanionDecision->TryIssueSupport(*F->Healer,*F->Owner,*Policy,EAetherCompanionSupportPurpose::FriendlyHealing));
    });
    Advance(.8);
    Step([F,this,Policy]{
        TestTrue(TEXT("Real Profile owner accepts formal aid"),F->Owner->BuffRuntime->GetState().Instances.Num()==1&&F->Healer->Mana()==85);
        auto* C=F->Probe;auto* D=C->CompanionDecision.Get();F->Patient->SetVitals(40,100,100);F->Stranger->SetVitals(30,100,100);
        TestFalse(TEXT("Different recruitment owner is not an AI patient"),D->TryIssueSupport(*C,*F->Stranger,*Policy,EAetherCompanionSupportPurpose::FriendlyHealing));
        F->Patient->SetActorLocation(FVector(10700,0,88));
        TestFalse(TEXT("Range comes from real aid effect and sweep"),D->TryIssueSupport(*C,*F->Patient,*Policy,EAetherCompanionSupportPurpose::FriendlyHealing));F->Patient->SetActorLocation(FVector(10300,0,88));
        F->Stranger->SetActorLocation(FVector(10150,0,88));
        TestFalse(TEXT("Real aim cannot silently heal a blocking unrelated actor"),D->TryIssueSupport(*C,*F->Patient,*Policy,EAetherCompanionSupportPurpose::FriendlyHealing));F->Stranger->SetActorLocation(FVector(10300,300,88));
        C->SetVitals(100,10,100);TestFalse(TEXT("GAS insufficient cost blocks intent"),D->TryIssueSupport(*C,*F->Patient,*Policy,EAetherCompanionSupportPurpose::FriendlyHealing));
        TestEqual(TEXT("Rejected patients/resources never debit"),C->Mana(),10.f);C->SetVitals(100,100,100);
        F->Patient->SetVitals(0,100,100);TestFalse(TEXT("Healing cannot replace downed rescue"),D->TryIssueSupport(*C,*F->Patient,*Policy,EAetherCompanionSupportPurpose::FriendlyHealing));F->Patient->SetVitals(40,100,100);
    });
    for(int32 Mode=0;Mode<5;++Mode)
    {
        Step([F,this,Policy,Mode]{
            auto* C=F->Probe;auto* D=C->CompanionDecision.Get();D->CancelOwnedSupport();C->bCompanionHold=false;F->Patient->CompanionOwner=F->Owner;
            C->SetVitals(100,100,100);F->Patient->SetVitals(40,100,100);
            TestTrue(TEXT("Real intent enters windup before interruption"),D->TryIssueSupport(*C,*F->Patient,*Policy,EAetherCompanionSupportPurpose::FriendlyHealing)&&C->CastExecutionId.IsValid());F->OldExecution=C->CastExecutionId;
            if(Mode==0){C->bCompanionHold=true;D->TickComponent(.01f,LEVELTICK_All,nullptr);TestFalse(TEXT("Hold cancels only the owned real execution"),C->CastExecutionId.IsValid());}
            else if(Mode==1)
            {const auto Before=C->CombatRuntime->DamageReceivedCount;FDamageEvent Damage;C->TakeDamage(10,Damage,nullptr,nullptr);TestTrue(TEXT("Real damage advances interruption identity"),C->CombatRuntime->DamageReceivedCount>Before);}
            else if(Mode==2){F->ProbeController->SetFocus(F->Stranger,EAIFocusPriority::Gameplay);F->ProbeController->UpdateControlRotation(.05f,true);}
            else if(Mode==3)
            {
                FGuid OldLife,NewLife;AetherSkillLives::Resolve(*F->Patient,OldLife);
                F->Patient->SetVitals(0,100,100);F->Patient->SetVitals(40,100,100);
                TestTrue(TEXT("Same Patient actor really enters a new authoritative life"),AetherSkillLives::Resolve(*F->Patient,NewLife)&&OldLife!=NewLife);
            }
            else
            {
                F->Patient->CompanionOwner=F->Stranger;TestTrue(TEXT("Changed membership leaves real GA windup for Tick to cancel"),C->CastExecutionId==F->OldExecution);
                F->EndReentered=false;F->EndHook=C->AbilitySystem->OnAbilityEnded.AddLambda([F](const FAbilityEndedData& Data){if(Data.bWasCancelled&&!F->EndReentered){F->EndReentered=true;F->ProbeController->UnPossess();}});
                D->TickComponent(.01f,LEVELTICK_All,nullptr);C->AbilitySystem->OnAbilityEnded.Remove(F->EndHook);
                TestTrue(TEXT("Tick cancellation callback really unpossesses without stale-owner continuation"),F->EndReentered&&!C->GetController()&&!D->bManaged);
            }
        });
        Advance(.8);
        Step([F,this,Mode]{
            auto* C=F->Probe;
            if(Mode==4)
            {
                FGuid Life;TestFalse(TEXT("Control gap is not a usable skill life"),AetherSkillLives::Resolve(*C,Life));
                TestEqual(TEXT("Cooldown query is unavailable while ActorInfo is cleared"),C->SkillCooldownRemaining(TEXT("Body.Aid")),TNumericLimits<float>::Max());
                F->Patient->CompanionOwner=F->Owner;F->ProbeController->Possess(C);TestTrue(TEXT("New controller generation may resume after cancelled tick"),C->CompanionDecision->bManaged);
            }
            TestTrue(TEXT("Interrupted or redirected request never commits a heal/cooldown"),C->Mana()==100&&C->SkillCooldownRemaining(TEXT("Body.Aid"))==0&&F->Patient->BuffRuntime->GetState().Instances.IsEmpty()&&F->Stranger->BuffRuntime->GetState().Instances.IsEmpty());
            C->CompanionDecision->CancelOwnedSupport();C->bCompanionHold=false;
            if(Mode==2){TestTrue(TEXT("Releasing old intent does not clear another behavior's focus"),F->ProbeController->GetFocusActorForPriority(EAIFocusPriority::Gameplay)==F->Stranger);F->ProbeController->ClearFocus(EAIFocusPriority::Gameplay);}
        });
    }
    Step([F,this,Policy]{
        auto* C=F->Probe;auto* D=C->CompanionDecision.Get();C->SetVitals(40,100,100);
        TestTrue(TEXT("Real owned windup precedes controller replacement"),D->TryIssueSupport(*C,*F->Patient,*Policy,EAetherCompanionSupportPurpose::FriendlyHealing)&&C->CastExecutionId.IsValid());
        F->OldExecution=C->CastExecutionId;const auto Generation=D->ControlGeneration;FGuid OldLife,NewLife;AetherSkillLives::Resolve(*C,OldLife);
        F->ProbeController->UnPossess();F->ProbeController->Possess(C);
        TestTrue(TEXT("Controller handoff changes intent generation but preserves living body identity"),D->ControlGeneration>Generation&&AetherSkillLives::Resolve(*C,NewLife)&&OldLife==NewLife&&!C->CastExecutionId.IsValid());
        TestTrue(TEXT("Fresh ordinary self skill starts after handoff"),C->TrySkill(TEXT("Body.Mend"))&&C->CastExecutionId.IsValid());F->NewExecution=C->CastExecutionId;
        D->CancelOwnedSupport();TestTrue(TEXT("Old helper cleanup leaves new ordinary execution alone"),C->CastExecutionId==F->NewExecution&&F->NewExecution!=F->OldExecution);
    });
    Advance(1.7);
    Step([F,this]{
        auto* C=F->Probe;TestTrue(TEXT("Only post-handoff ordinary self cast completes"),C->Mana()==85&&C->BuffRuntime->GetState().Instances.Num()==1&&C->SkillCooldownRemaining(TEXT("Body.Aid"))==0&&F->Patient->BuffRuntime->GetState().Instances.IsEmpty());
        FString Why;C->BuffRuntime->Dispel(TEXT("Positive"),Why);F->Patient->SetVitals(40,100,100);C->SetVitals(100,100,100);
        TestTrue(TEXT("Prepare target due healing for commit reentrancy"),F->Patient->BuffRuntime->Apply(TEXT("Sample.Regeneration"),TEXT("Fixture.TargetDue"),Why));
    });
    Advance(1.6);
    Step([F,this,Policy]{
        F->Reentered=false;F->Debits=F->Refunds=0;
        F->HealthHook=F->Patient->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).AddLambda([F](const FOnAttributeChangeData& Change){if(Change.NewValue>Change.OldValue&&!F->Reentered){F->Reentered=true;F->Patient->CompanionOwner=F->Stranger;}});
        F->ManaHook=F->Probe->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).AddLambda([F](const FOnAttributeChangeData& Change){if(Change.NewValue<Change.OldValue)++F->Debits;else if(Change.NewValue>Change.OldValue)++F->Refunds;});
        TestTrue(TEXT("Original recruitment starts actual friendly windup"),F->Probe->CompanionDecision->TryIssueSupport(*F->Probe,*F->Patient,*Policy,EAetherCompanionSupportPurpose::FriendlyHealing));
    });
    Advance(.8);
    Step([F,this]{
        auto* C=F->Probe;F->Patient->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).Remove(F->HealthHook);
        C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).Remove(F->ManaHook);
        TestTrue(TEXT("Target FlushDue callback really changed membership after payment"),F->Reentered&&F->Patient->Health()==44&&F->Debits==1&&F->Refunds==1);
        TestTrue(TEXT("Formal pre-effect policy rejects changed membership without a second buff"),C->Mana()==100&&C->SkillCooldownRemaining(TEXT("Body.Aid"))==0&&F->Patient->BuffRuntime->GetState().Instances.Num()==1);
        C->CompanionDecision->CancelOwnedSupport();F->Patient->CompanionOwner=F->Owner;
        FString Why;F->Patient->BuffRuntime->Dispel(TEXT("Positive"),Why);F->Patient->Reactive->State.TemperatureC=60;
        C->WaterReserveKg=3;F->Debits=F->Refunds=0;F->Reentered=F->ReplacementAccepted=false;F->OldExecution.Invalidate();F->NewExecution.Invalidate();
        F->ManaHook=C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).AddLambda([F](const FOnAttributeChangeData& Change){
            if(Change.NewValue>Change.OldValue){++F->Refunds;return;}if(Change.NewValue>=Change.OldValue)return;++F->Debits;auto* C=F->Probe;
            if(F->Reentered){F->NewExecution=C->CastExecutionId;return;}
            F->Reentered=true;F->OldExecution=C->CastExecutionId;C->CompanionDecision->EndCompanionControl();
            C->GetController()->SetControlRotation((F->Stranger->GetActorLocation()-C->SkillAimOrigin()).Rotation());
            F->ReplacementAccepted=C->TrySkill(TEXT("Water.Draw"));
        });
    });
    Step([F,this,Policy]{
        auto* C=F->Probe; // 请求前重新开始真实AI控制；支付回调会结束它并重开普通施法。
        TestTrue(TEXT("Restart managed control for synchronous cooling fixture"),C->CompanionDecision->BeginCompanionControl());
        C->CompanionDecision->TryIssueSupport(*C,*F->Patient,*Policy,EAetherCompanionSupportPurpose::Cooling);
        C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).Remove(F->ManaHook);
        TestTrue(TEXT("Zero-windup callback owns two distinct real executions"),F->Reentered&&F->ReplacementAccepted&&F->OldExecution.IsValid()&&F->NewExecution.IsValid()&&F->OldExecution!=F->NewExecution);
        TestTrue(TEXT("Canceled AI refunds only itself and does not capture ordinary replacement"),F->Debits==2&&F->Refunds==1&&C->Mana()==88&&!C->CompanionDecision->SupportIntent);
        TestTrue(TEXT("Only accepted ordinary replacement consumes authored water"),FMath::IsNearlyEqual(C->WaterReserveKg,2.5f)&&C->SkillCooldownRemaining(TEXT("Water.Draw"))>0);
        TestTrue(TEXT("Cancellation tombstone belongs to original execution"),C->CompanionDecision->CanceledSupportExecution==F->OldExecution);
    });
    Advance(.8);
    Step([F,this,Policy]{
        auto* C=F->Probe;C->SetVitals(100,100,100);TestTrue(TEXT("Resume helper without changing ability ownership"),C->CompanionDecision->BeginCompanionControl());
        F->Reentered=F->EndReentered=F->RestartedControl=F->ReplacementAccepted=false;F->Debits=F->Refunds=0;F->OldExecution.Invalidate();F->NewExecution.Invalidate();
        const auto* Water=AetherSkillBinding::Find(*C->AbilitySystem,TEXT("Water.Draw"));if(!TestNotNull(TEXT("Real cooling spec for End callback"),Water))return;const auto Handle=Water->Handle;
        F->EndHook=C->AbilitySystem->OnAbilityEnded.AddLambda([F,Handle](const FAbilityEndedData& Data){
            if(!Data.bWasCancelled||Data.AbilitySpecHandle!=Handle||F->EndReentered)return;F->EndReentered=true;auto* C=F->Probe;
            F->RestartedControl=C->CompanionDecision->BeginCompanionControl();
            F->ProbeController->SetFocus(F->Stranger,EAIFocusPriority::Gameplay);F->ProbeController->UpdateControlRotation(.05f,true);
            F->ReplacementAccepted=C->TrySkill(TEXT("Body.Aid"));F->NewExecution=C->CastExecutionId;
        });
        F->ManaHook=C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).AddLambda([F](const FOnAttributeChangeData& Change){
            if(Change.NewValue>Change.OldValue){++F->Refunds;return;}if(Change.NewValue>=Change.OldValue)return;++F->Debits;
            if(F->Reentered)return;F->Reentered=true;F->OldExecution=F->Probe->CastExecutionId;F->Probe->CompanionDecision->EndCompanionControl();
        });
        C->CompanionDecision->TryIssueSupport(*C,*F->Patient,*Policy,EAetherCompanionSupportPurpose::Cooling);
        C->AbilitySystem->OnAbilityEnded.Remove(F->EndHook);
        TestTrue(TEXT("Actual End callback restarts control and a distinct ordinary execution"),F->EndReentered&&F->RestartedControl&&C->CompanionDecision->bManaged&&F->ReplacementAccepted&&F->NewExecution.IsValid()&&F->OldExecution!=F->NewExecution&&C->CastExecutionId==F->NewExecution);
        C->CompanionDecision->EndCompanionControl();TestTrue(TEXT("Stopping helper leaves non-owned action and external focus alive"),C->CastExecutionId==F->NewExecution&&F->ProbeController->GetFocusActorForPriority(EAIFocusPriority::Gameplay)==F->Stranger);
    });
    Advance(.8);
    Step([F,this]{
        auto* C=F->Probe;C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetManaAttribute()).Remove(F->ManaHook);
        TestTrue(TEXT("End-callback ordinary healing alone survives and pays"),F->Debits==2&&F->Refunds==1&&C->Mana()==85&&F->Stranger->BuffRuntime->GetState().Instances.Num()==1);
        TestTrue(TEXT("Canceled cooling did not spend water or receive cooldown"),FMath::IsNearlyEqual(C->WaterReserveKg,2.5f)&&C->SkillCooldownRemaining(TEXT("Water.Draw"))==0);
        C->SkillLoadoutId=TEXT("Fixture.MissingSupport");C->CompanionDecision->BeginCompanionControl();C->CompanionDecision->TickComponent(.01f,LEVELTICK_All,nullptr);
        TestTrue(TEXT("Missing support configuration is diagnosed without disabling follow/control"),C->CompanionDecision->bManaged&&C->CompanionDecision->IsComponentTickEnabled()&&!C->CompanionDecision->SupportDiagnostic.IsEmpty());
        C->SkillLoadoutId=TEXT("Companion.Healer");
    });
    Step([F]{auto* W=F->World;F->World=nullptr;W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);W->RemoveFromRoot();});
    return true;
}
#endif
