#include "Misc/AutomationTest.h"
#include "Movement/AetherVaultAbility.h"
#include "Framework/AetherFrontier.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/RootMotionSource.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherVaultLifecycleTest,"Aether.V10.Movement.VaultInterruptionOwnsPhysicsNotPresentation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherVaultLifecycleTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated vault lifecycle world"),W))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT {W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false);};
    auto BoxAt=[&](FVector Position,FVector Extent)
    {
        auto* A=W->SpawnActor<AActor>();auto* B=NewObject<UBoxComponent>(A);
        A->SetRootComponent(B);A->AddInstanceComponent(B);B->SetBoxExtent(Extent);
        B->SetWorldLocation(Position);B->SetMobility(EComponentMobility::Static);
        B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);B->SetCollisionObjectType(ECC_WorldStatic);
        B->SetCollisionResponseToAllChannels(ECR_Block);B->RegisterComponent();
    };
    BoxAt(FVector(0,0,-10),FVector(800,800,10));
    // A thin, static 70cm obstacle with full capsule clearance on both sides.
    BoxAt(FVector(80,0,35),FVector(20,500,35));
    int32 Index=0;
    auto MakeCharacter=[&](float Y)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* C=W->SpawnActor<AAetherFrontierCharacter>(FVector(0,Y,90),FRotator::ZeroRotator,Params);
        auto* PS=W->SpawnActor<AAetherPlayerState>();PS->Profile.CharacterId=FString::Printf(TEXT("VaultLifecycle%d"),++Index);
        C->SetPlayerState(PS);C->BindPersistentAbilities();C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());
        C->SetVitals(100,100,100);C->GrantSpells();C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        return C;
    };
    auto* Hit=MakeCharacter(-300);auto* Downed=MakeCharacter(-100);auto* External=MakeCharacter(100);auto* Swapped=MakeCharacter(300);
    ON_SCOPE_EXIT {for(auto* C:{Hit,Downed,External,Swapped})C->CancelActions();};
    auto Start=[&](AAetherFrontierCharacter* C)->UAetherVaultAbility*
    {
        if(!TestTrue(TEXT("Production GAS vault activates through real obstacle query"),C->AbilitySystem->TryActivateAbilityByClass(UAetherVaultAbility::StaticClass())))return nullptr;
        auto* Spec=C->AbilitySystem->FindAbilitySpecFromClass(UAetherVaultAbility::StaticClass());
        auto* Ability=Spec?Cast<UAetherVaultAbility>(Spec->GetPrimaryInstance()):nullptr;
        TestNotNull(TEXT("Instanced vault ability"),Ability);
        TestEqual(TEXT("Vault temporarily enters flying"),C->GetCharacterMovement()->MovementMode.GetValue(),MOVE_Flying);
        TestTrue(TEXT("First production root source exists"),C->GetCharacterMovement()->GetRootMotionSource(TEXT("Aether.Vault.0")).IsValid());
        return Ability;
    };
    auto CheckEnded=[&](AAetherFrontierCharacter* C,UAetherVaultAbility* Ability)
    {
        TestFalse(TEXT("Vault ability ended"),Ability->IsActive());
        const auto Source=C->GetCharacterMovement()->GetRootMotionSource(TEXT("Aether.Vault.0"));
        TestTrue(TEXT("First root source removed or queued for movement cleanup"),
            !Source.IsValid()||Source->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
        TestFalse(TEXT("Interruption timer cleared"),W->GetTimerManager().IsTimerActive(Ability->Watch));
        TestFalse(TEXT("No retained flying ownership"),Ability->bOwnsFlyingMode);
    };
    FDamageEvent Damage;
    auto* HitAbility=Start(Hit);if(!HitAbility)return false;
    const uint32 VaultSerial=Hit->PresentedAction.Serial;
    Hit->TakeDamage(10,Damage,nullptr,nullptr);
    TestEqual(TEXT("Nonlethal damage publishes Hit"),Hit->PresentedAction.Id,FName(TEXT("Hit")));
    TestTrue(TEXT("Hit replaces vault presentation serial"),Hit->PresentedAction.Serial!=VaultSerial);
    // Exercise the production interruption predicate directly; this is not a timer cadence test.
    HitAbility->CheckInterruption();CheckEnded(Hit,HitAbility);
    TestEqual(TEXT("Hit interruption restores gravity"),Hit->GetCharacterMovement()->MovementMode.GetValue(),MOVE_Falling);
    TestTrue(TEXT("Cleanup preserves newer Hit presentation"),Hit->PresentedAction.Id==TEXT("Hit")&&Hit->PresentedAction.Duration>0);
    Hit->GetCharacterMovement()->SetMovementMode(MOVE_Walking);HitAbility->Abort();HitAbility->Abort();
    TestEqual(TEXT("Repeated end cannot alter later mode"),Hit->GetCharacterMovement()->MovementMode.GetValue(),MOVE_Walking);

    auto* DownedAbility=Start(Downed);if(!DownedAbility)return false;
    Downed->TakeDamage(200,Damage,nullptr,nullptr);CheckEnded(Downed,DownedAbility);
    TestEqual(TEXT("Death never restores movable mode"),Downed->GetCharacterMovement()->MovementMode.GetValue(),MOVE_None);

    auto* ExternalAbility=Start(External);if(!ExternalAbility)return false;
    External->GetCharacterMovement()->SetMovementMode(MOVE_Swimming);CheckEnded(External,ExternalAbility);
    TestEqual(TEXT("External mode is not overwritten"),External->GetCharacterMovement()->MovementMode.GetValue(),MOVE_Swimming);
    External->GetCharacterMovement()->SetMovementMode(MOVE_Flying);ExternalAbility->Abort();
    TestEqual(TEXT("Later external flying is not owned by ended vault"),External->GetCharacterMovement()->MovementMode.GetValue(),MOVE_Flying);

    auto* SwappedAbility=Start(Swapped);if(!SwappedAbility)return false;
    auto* System=Swapped->AbilitySystem.Get();auto* Replacement=MakeCharacter(650);
    const auto ReplacementMode=Replacement->GetCharacterMovement()->MovementMode.GetValue();
    System->InitAbilityActorInfo(Swapped->ProfileState(),Replacement);
    SwappedAbility->CheckInterruption();CheckEnded(Swapped,SwappedAbility);
    TestEqual(TEXT("Avatar replacement cleans original movement"),Swapped->GetCharacterMovement()->MovementMode.GetValue(),MOVE_Falling);
    TestEqual(TEXT("Avatar replacement does not alter new pawn"),Replacement->GetCharacterMovement()->MovementMode.GetValue(),ReplacementMode);
    return true;
}
#endif
