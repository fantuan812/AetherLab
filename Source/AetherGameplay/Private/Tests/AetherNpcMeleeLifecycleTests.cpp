#include "Misc/AutomationTest.h"
#include "AI/AetherNpcMeleeDecision.h"
#include "AI/AetherNpcMeleeDefinitions.h"
#include "Combat/AetherCombat.h"
#include "Interaction/AetherActions.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotionConstantForce.h"
#include "AetherEquipmentComponent.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNpcMeleeLifecycleTest,"Aether.AI.Melee.AuthoritativeExecutionAndOwnedMotion",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNpcMeleeLifecycleTest::RunTest(const FString&)
{
    const auto* Authored=FAetherNpcMeleeDefinitions::Get().ForFighter(TEXT("Wolf"));
    if(!TestNotNull(TEXT("Production wolf content"),Authored))return false;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated melee world"),World))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Npc=World->SpawnActor<AAetherCharacter>(FVector(0,0,88),FRotator::ZeroRotator,Params);
    auto* Target=World->SpawnActor<AAetherCharacter>(FVector(100,0,88),FRotator::ZeroRotator,Params);
    auto* AlternateAvatar=World->SpawnActor<AAetherCharacter>(FVector(3000,0,88),FRotator::ZeroRotator,Params);
    auto* First=World->SpawnActor<AAIController>();auto* Second=World->SpawnActor<AAIController>();
    if(!TestNotNull(TEXT("NPC"),Npc)||!TestNotNull(TEXT("Target"),Target)||!TestNotNull(TEXT("Alternate ASC avatar"),AlternateAvatar)||!TestNotNull(TEXT("Controller"),First)||!TestNotNull(TEXT("Replacement controller"),Second))return false;
    for(auto* C:{Npc,Target})
    {
        C->bUseBasicAssets=true;C->SkillLoadoutId=TEXT("MeleeOnly");C->Home=C->GetActorLocation();
        C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());C->AbilitySystem->InitAbilityActorInfo(C,C);C->SetVitals(100,100,100);C->GrantCoreAbilities();
    }
    Npc->Fighter=EAetherFighter::Wolf;Target->DispatchBeginPlay();First->Possess(Npc);
    auto* E=Npc->Equipment.Get();auto* Movement=Npc->GetCharacterMovement();Movement->SetMovementMode(MOVE_Walking);
    E->CanAct.BindUObject(Npc,&AAetherCharacter::Ready);E->RequestAttack.BindUObject(Npc,&AAetherCharacter::RequestMelee);
    E->CanContinueAttack.BindLambda([&]{return Npc->Alive()&&Npc->AbilitySystem->GetAvatarActor()==Npc;});
    auto* Catalog=NewObject<UAetherEquipmentCatalog>(Npc);auto* Item=NewObject<UAetherEquipmentDefinition>(Catalog);
    Item->ItemId=TEXT("Fixture.Melee");Item->Slot=TEXT("MainHand");Item->AllowedSlots={TEXT("MainHand")};
    Item->Mesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
    FAetherAttackDefinition Light;Light.Id=TEXT("Light");Light.Damage=Light.PostureDamage=0;Light.ReachCm=120;
    Light.StaminaCost=8;Light.WindupSeconds=.1f;Light.ActiveSeconds=.4f;Light.RecoverySeconds=1.5f;
    auto Heavy=Light;Heavy.Id=TEXT("Heavy");Heavy.ReachCm=320;Item->Attacks={Light,Heavy};Catalog->Items={Item};E->Catalog=Catalog;
    FAetherEquippedSlot Slot;Slot.Slot=TEXT("MainHand");Slot.ItemId=Item->ItemId;
    if(!TestTrue(TEXT("Real equipment loadout validates and installs"),E->RestoreLoadout({Slot})))return false;
    World->SetBegunPlay(true);
    const auto Advance=[&](float Seconds){World->Tick(LEVELTICK_TimeOnly,Seconds);};
    const auto Ability=[&](int32 Level)->UAetherMeleeAbility*
    {
        for(const auto& Spec:Npc->AbilitySystem->GetActivatableAbilities())
            if(Spec.Ability&&Spec.Ability->IsA<UAetherMeleeAbility>()&&Spec.Level==Level)return Cast<UAetherMeleeAbility>(Spec.GetPrimaryInstance());
        return nullptr;
    };
    auto& Decision=Npc->EnemyMeleeDecision;auto Policy=*Authored;Policy.Id=TEXT("Fixture.Immediate");Policy.HealthBands={{1,0}};
    const auto Prepare=[&](){return Decision.Choose(*Npc,*Target,Target->GetActorLocation(),Policy)==EAetherNpcMeleeChoice::Ready;};
    const auto NoMotion=[&](UAetherMeleeAbility* GA){return !Movement->HasRootMotionSources()&&(!GA||!GA->OwnedMotion)&&Movement->PendingLaunchVelocity.IsNearlyZero();};
    const auto SourceName=[](FGuid Id){return FName(*(TEXT("Aether.NpcMelee.")+Id.ToString(EGuidFormats::Digits)));};
    const auto Removed=[&](FName Name)
    {const auto Source=Movement->GetRootMotionSource(Name);return !Source.IsValid()||Source->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval);};

    // 真正Think电报到期后触发低资源GAS拒绝，不能只在helper入口测试false。
    Npc->SetVitals(100,100,0);const uint64 Before=E->AcceptedAttackCount;Npc->Think(.01f);
    TestTrue(TEXT("Canonical Think starts the authored telegraph"),Npc->bWindingUp);
    Advance(1);Npc->Think(.01f);
    TestTrue(TEXT("Rejected real Think attack neither launches nor invents recovery"),E->AcceptedAttackCount==Before&&Npc->ActionUntil==0&&NoMotion(Ability(1)));
    Npc->SetVitals(100,100,100);Decision.Reset();
    Target->SetActorLocation(FVector(200,0,88));Policy.AttackId=TEXT("Heavy");
    TestTrue(TEXT("Heavy uses its own real reach rather than Light reach"),Prepare());Decision.Reset();Policy.AttackId=TEXT("Light");
    TestTrue(TEXT("Same distance is outside the actual Light reach"),Decision.Choose(*Npc,*Target,Target->GetActorLocation(),Policy)==EAetherNpcMeleeChoice::Approach);
    if(!TestTrue(TEXT("Remove actual weapon"),E->RestoreLoadout({})))return false;
    Target->SetActorLocation(FVector(100,0,88));
    TestTrue(TEXT("No weapon cannot borrow a 130cm attack fallback"),Decision.Choose(*Npc,*Target,Target->GetActorLocation(),Policy)==EAetherNpcMeleeChoice::Unavailable);
    E->RestoreLoadout({Slot});Item->Attacks.RemoveAt(1);Policy.AttackId=TEXT("Heavy");
    TestTrue(TEXT("Missing Heavy definition cannot borrow Light"),Decision.Choose(*Npc,*Target,Target->GetActorLocation(),Policy)==EAetherNpcMeleeChoice::Unavailable);
    Item->Attacks.Add(Heavy);Policy.AttackId=TEXT("Light");

    int32 Debits=0,Refunds=0;bool CancelledDuringPayment=false;
    const auto Payment=Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetStaminaAttribute()).AddLambda([&](const FOnAttributeChangeData& Change)
    {
        if(Change.NewValue<Change.OldValue){++Debits;if(!CancelledDuringPayment){CancelledDuringPayment=true;Npc->CancelActions();}}
        else if(Change.NewValue>Change.OldValue)++Refunds;
    });
    if(!TestTrue(TEXT("Real pending intent before payment cancellation"),Prepare()))return false;
    TestFalse(TEXT("Payment cancellation cannot report an active committed attack"),Decision.TryExecute(*Npc));
    Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetStaminaAttribute()).Remove(Payment);
    TestTrue(TEXT("Payment branch was entered and refunded exactly once"),CancelledDuringPayment&&Debits==1&&Refunds==1&&Npc->Stamina()==100);
    TestTrue(TEXT("Canceled payment never creates physical motion or another recovery timer"),NoMotion(Ability(1))&&Npc->ActionUntil==0);

    bool ChangedLoadout=false;Debits=Refunds=0;
    const auto EquipDuringPayment=Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetStaminaAttribute()).AddLambda([&](const FOnAttributeChangeData& Change)
    {
        if(Change.NewValue<Change.OldValue){++Debits;if(!ChangedLoadout){ChangedLoadout=true;E->RestoreLoadout({});}}
        else if(Change.NewValue>Change.OldValue)++Refunds;
    });
    if(!Prepare())return false;TestFalse(TEXT("Payment-time equipment mutation is rejected"),Decision.TryExecute(*Npc));
    Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetStaminaAttribute()).Remove(EquipDuringPayment);
    TestTrue(TEXT("Real mutation reached debit then refund without motion"),ChangedLoadout&&Debits==1&&Refunds==1&&NoMotion(Ability(1)));E->RestoreLoadout({Slot});

    bool MovedTarget=false;Debits=Refunds=0;
    const auto MoveDuringPayment=Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetStaminaAttribute()).AddLambda([&](const FOnAttributeChangeData& Change)
    {
        if(Change.NewValue<Change.OldValue){++Debits;if(!MovedTarget){MovedTarget=true;Target->SetActorLocation(FVector(1000,0,88));}}
        else if(Change.NewValue>Change.OldValue)++Refunds;
    });
    if(!Prepare())return false;TestFalse(TEXT("Post-payment target distance is revalidated"),Decision.TryExecute(*Npc));
    Npc->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetStaminaAttribute()).Remove(MoveDuringPayment);
    TestTrue(TEXT("Rejected changed target reached debit and refund without motion"),MovedTarget&&Debits==1&&Refunds==1&&NoMotion(Ability(1)));
    Target->SetActorLocation(FVector(100,0,88));

    bool CancelledWindup=false;const uint64 BeforeWindup=E->AcceptedAttackCount;
    const auto Windup=E->OnAttackPhaseChanged.AddLambda([&](uint32,EAetherAttackPhase Phase)
    {if(Phase==EAetherAttackPhase::Windup&&!CancelledWindup){CancelledWindup=true;Npc->CancelActions();}});
    if(!Prepare())return false;TestFalse(TEXT("Accepted counter increment is not a success receipt after synchronous Windup cancellation"),Decision.TryExecute(*Npc));
    E->OnAttackPhaseChanged.Remove(Windup);
    TestTrue(TEXT("The real accepted attack was canceled before motion"),CancelledWindup&&E->AcceptedAttackCount==BeforeWindup+1&&!E->IsBusy()&&NoMotion(Ability(1)));
    TestEqual(TEXT("After actual Windup acceptance cancellation keeps the committed cost"),Npc->Stamina(),92.f);
    Npc->SetVitals(100,100,100);

    if(!TestTrue(TEXT("Valid intent reaches actual GAS commit"),Prepare()&&Decision.TryExecute(*Npc)))return false;
    auto* GA=Ability(1);if(!TestNotNull(TEXT("Instanced formal melee ability"),GA))return false;
    const FGuid Execution=GA->ActiveExecution();const uint32 Serial=E->Attack.Serial;
    TestTrue(TEXT("Receipt is the current actual equipment execution"),Execution.IsValid()&&Execution==E->Attack.ExecutionId&&E->Attack.AttackId==TEXT("Light"));
    TestTrue(TEXT("Successful Windup still cannot start an Active-only motion"),!GA->OwnedMotion&&Movement->PendingLaunchVelocity.IsNearlyZero());
    Advance(.15f);E->TickComponent(.15f,LEVELTICK_All,nullptr);
    auto* Motion=GA->OwnedMotion.Get();const auto Source=Movement->GetRootMotionSource(SourceName(Execution));
    if(!TestTrue(TEXT("Real equipment Active owns a real GAS task and root motion source"),Motion&&!Motion->IsFinished()&&Source.IsValid()))return false;
    TestTrue(TEXT("Finish mode never sets or clamps a later controller velocity"),Source->FinishVelocityParams.Mode==ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity);
    GA->AttackPhaseChanged(Serial,EAetherAttackPhase::Active);
    TestTrue(TEXT("Repeated same-execution phase cannot duplicate the task"),GA->OwnedMotion.Get()==Motion);
    const auto CommittedFacing=Npc->GetActorRotation();const auto CommittedControl=Npc->GetController()->GetControlRotation();
    Target->SetActorLocation(FVector(0,100,88));Npc->Think(.01f);
    TestTrue(TEXT("A busy authoritative attack cannot track the new target bearing"),Npc->GetActorRotation().Equals(CommittedFacing)&&Npc->GetController()->GetControlRotation().Equals(CommittedControl));
    Target->SetActorLocation(FVector(100,0,88));
    Npc->CancelActions();
    TestTrue(TEXT("Cancel ends its task and marks only its source for removal"),Motion->IsFinished()&&!GA->OwnedMotion&&Removed(SourceName(Execution))&&!GA->ActiveExecution().IsValid());
    // 源的MarkedForRemoval按引擎合同在下次Prepare清理；这里不声称已执行物理积分。
    Npc->SetVitals(100,100,100);if(!Prepare()||!Decision.TryExecute(*Npc))return false;
    const FGuid Next=GA->ActiveExecution();Advance(.15f);E->TickComponent(.15f,LEVELTICK_All,nullptr);auto* NewMotion=GA->OwnedMotion.Get();
    TestTrue(TEXT("New execution starts only after old owned task has finished"),Motion->IsFinished()&&Next.IsValid()&&Next!=Execution&&NewMotion&&NewMotion!=Motion);
    Second->Possess(Npc);
    TestTrue(TEXT("Controller replacement ends old task and execution"),NewMotion->IsFinished()&&!GA->OwnedMotion&&Removed(SourceName(Next))&&!E->IsBusy());

    Npc->SetVitals(100,100,100);Movement->SetMovementMode(MOVE_Walking);
    if(!Prepare()||!Decision.TryExecute(*Npc))return false;
    const FGuid BeforeAvatar=GA->ActiveExecution();Advance(.15f);E->TickComponent(.15f,LEVELTICK_All,nullptr);auto* AvatarMotion=GA->OwnedMotion.Get();
    if(!TestNotNull(TEXT("Actual active task before ASC avatar replacement"),AvatarMotion))return false;
    auto* OriginalSystem=Npc->AbilitySystem.Get();OriginalSystem->InitAbilityActorInfo(Npc,AlternateAvatar);
    TestTrue(TEXT("Real ASC avatar replacement ends only the old body's execution"),OriginalSystem->GetAvatarActor()==AlternateAvatar&&AvatarMotion->IsFinished()&&
        !GA->OwnedMotion&&Removed(SourceName(BeforeAvatar))&&!E->IsBusy()&&!AlternateAvatar->GetCharacterMovement()->HasRootMotionSources());
    OriginalSystem->InitAbilityActorInfo(Npc,Npc);

    // 用真正ASC tag委托同步结束旧执行并创建新执行，验证旧相位栈不能操作新成员。
    const auto ActiveTag=FGameplayTag::RequestGameplayTag(TEXT("Aether.Action.Melee.Active"));
    const auto WindupTag=FGameplayTag::RequestGameplayTag(TEXT("Aether.Action.Melee.Windup"));
    for(bool OnRemoval:{false,true})
    {
        Npc->SetVitals(100,100,100);Movement->SetMovementMode(MOVE_Walking);
        bool Reentered=false,AcceptedReplacement=false;FGuid Replacement;
        const auto Tag=OnRemoval?WindupTag:ActiveTag;
        const auto Hook=Npc->AbilitySystem->RegisterGameplayTagEvent(Tag,EGameplayTagEventType::NewOrRemoved).AddLambda([&](const FGameplayTag,int32 Count)
        {
            if(Reentered||(OnRemoval?Count!=0:Count==0))return;
            Reentered=true;Npc->CancelActions();Npc->SetVitals(100,100,100);
            AcceptedReplacement=Prepare()&&Decision.TryExecute(*Npc);Replacement=GA->ActiveExecution();
        });
        if(!Prepare()||!Decision.TryExecute(*Npc))return false;const auto Old=GA->ActiveExecution();
        Advance(.15f);E->TickComponent(.15f,LEVELTICK_All,nullptr);
        Npc->AbilitySystem->RegisterGameplayTagEvent(Tag,EGameplayTagEventType::NewOrRemoved).Remove(Hook);
        TestTrue(TEXT("Tag callback really replaced the active execution"),Reentered&&AcceptedReplacement&&Replacement.IsValid()&&Replacement!=Old);
        TestTrue(TEXT("Old phase continuation cannot start motion in new Windup"),E->Attack.Phase==EAetherAttackPhase::Windup&&!GA->OwnedMotion&&GA->PhaseTag==WindupTag&&Npc->AbilitySystem->GetTagCount(WindupTag)==1);
        Npc->CancelActions();
        TestEqual(TEXT("New execution still owns and removes its exact Windup tag"),Npc->AbilitySystem->GetTagCount(WindupTag),0);
    }
    Npc->SetVitals(100,100,100);Movement->SetMovementMode(MOVE_Walking);if(!Prepare()||!Decision.TryExecute(*Npc))return false;
    const FGuid Late=GA->ActiveExecution();Advance(.6f);E->TickComponent(.6f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Frame crossing the complete Active window cannot add a late lunge"),!GA->OwnedMotion&&!Movement->GetRootMotionSource(SourceName(Late)).IsValid());
    TestTrue(TEXT("Long authored equipment recovery remains the only busy authority"),E->IsBusy()&&E->Attack.Phase==EAetherAttackPhase::Recovery&&Npc->ActionUntil==0);
    Npc->Think(.01f);TestFalse(TEXT("Think cannot start a new telegraph while real recovery owns the action"),Npc->bWindingUp);
    Advance(1.5f);E->TickComponent(1.5f,LEVELTICK_All,nullptr);
    TestFalse(TEXT("Real equipment completion releases the GAS execution"),E->IsBusy()||GA->ActiveExecution().IsValid());
    Npc->SetVitals(100,100,100);E->ActionSpeed.BindLambda([]{return 2.f;});
    if(!Prepare()||!Decision.TryExecute(*Npc))return false;
    const auto* Scaled=E->CurrentAttack();
    TestTrue(TEXT("Actual committed equipment snapshot owns the speed-scaled recovery"),Scaled&&FMath::IsNearlyEqual(Scaled->RecoverySeconds,Light.RecoverySeconds/2));
    Advance(.3f);E->TickComponent(.3f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Scaled recovery remains busy beyond its active window"),E->IsBusy()&&E->Attack.Phase==EAetherAttackPhase::Recovery&&Npc->ActionUntil==0);
    Advance(.8f);E->TickComponent(.8f,LEVELTICK_All,nullptr);E->ActionSpeed.Unbind();
    TestFalse(TEXT("Scaled authoritative completion releases without a second AI timer"),E->IsBusy()||GA->ActiveExecution().IsValid());
    Npc->SetVitals(100,100,100);Npc->Fighter=EAetherFighter::Player;
    if(!TestTrue(TEXT("Ordinary player still uses formal melee entry"),Npc->RequestMelee(TEXT("Light"))))return false;
    Advance(.15f);E->TickComponent(.15f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Ordinary melee without NPC intent never borrows NPC motion"),!GA->NpcIntent.IsSet()&&!GA->OwnedMotion&&Movement->PendingLaunchVelocity.IsNearlyZero());
    Npc->CancelActions();return true;
}
#endif
