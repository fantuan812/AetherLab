#include "Characters/AetherFrontierCharacter.h"
#include "Networking/AetherCommandClient.h"
#include "Engine/LocalPlayer.h"
#include "Persistence/AetherNativePersistence.h"
#include "Engine/GameInstance.h"
#include "Quests/AetherGuide.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "Interaction/AetherWorldActionComponent.h"
#include "Framework/AetherFrontier.h"
#include "Inventory/AetherNativeInventory.h"
#include "Inventory/AetherResourceGate.h"
#include "Assets/AetherContent.h"
#include "Definitions/AetherV10Definitions.h"
#include "Definitions/AetherRules.h"
#include "Inventory/AetherInventoryRules.h"
#include "Interaction/AetherActions.h"
#include "Input/AetherPlayerInputComponent.h"
#include "Presentation/AetherPlayerPreferences.h"
#include "Movement/AetherCharacterMovement.h"
#include "Movement/AetherVaultAbility.h"
#include "GameFramework/SpringArmComponent.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "ReactiveWorldSubsystem.h"
#include "Combat/AetherControlledActionDefinition.h"
#include "Characters/AetherCompanionComponent.h"
#include "Combat/AetherDerivedStats.h"

AAetherFrontierCharacter::AAetherFrontierCharacter(const FObjectInitializer& ObjectInitializer)
    :Super(ObjectInitializer.SetDefaultSubobjectClass<UAetherCharacterMovement>(ACharacter::CharacterMovementComponentName))
{ SkillAuthority=EAetherSkillAuthority::Profile;bUseBasicAssets=true;JumpMaxCount=1;JumpMaxHoldTime=.18f;CarryHandle=CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("CarryHandle"));WorldActions=CreateDefaultSubobject<UAetherWorldActionComponent>(TEXT("WorldActions"));PlayerInput=CreateDefaultSubobject<UAetherPlayerInputComponent>(TEXT("PlayerInput"));CompanionDecision=CreateDefaultSubobject<UAetherCompanionComponent>(TEXT("CompanionDecision")); }
AAetherPlayerState* AAetherFrontierCharacter::ProfileState() const {return GetPlayerState<AAetherPlayerState>();}
void AAetherFrontierCharacter::BindPersistentAbilities()
{
    if (auto* PS=ProfileState())
    {
        if(HasAuthority())
        {
            if(AbilitySystem!=PS->AbilitySystem)
            {CancelActions();if(AbilitySystem&&AbilitySystem->GetAvatarActor()==this)AbilitySystem->ClearActorInfo();}
            if(auto* Old=Cast<AAetherCharacter>(PS->AbilitySystem->GetAvatarActor());Old&&Old!=this)Old->CancelActions();
        }
        AbilitySystem=PS->AbilitySystem; Attributes=PS->Attributes; AbilitySystem->InitAbilityActorInfo(PS,this);
        if(HasAuthority()&&DerivedAttributeSystem.Get()!=AbilitySystem)
        {
            if(DerivedAttributeSystem.IsValid())DerivedAttributeSystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetGearMaxHealthAttribute()).Remove(DerivedHealthDelegate);
            DerivedAttributeSystem=AbilitySystem;
            DerivedHealthDelegate=AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetGearMaxHealthAttribute()).AddWeakLambda(this,[this](const FOnAttributeChangeData&)
            {
                // GAS replaces source effects in several steps. Never clamp against an intermediate removal.
                if(bDerivedHealthQueued)return;bDerivedHealthQueued=true;
                GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this]{bDerivedHealthQueued=false;RefreshDerivedHealth();}));
            });
        }
        if(HasAuthority())PS->RefreshTemporarySkills();
    }
}
void AAetherFrontierCharacter::RefreshDerivedHealth()
{
    auto* PS=ProfileState();const auto* P=PS?PS->GetNativeProfile():nullptr;
    if(!HasAuthority()||!P||!AbilitySystem||AbilitySystem->GetAvatarActor()!=this)return;
    // Re-evaluate after the barrier instead of queueing a stale absolute limit or health value.
    const TWeakObjectPtr<AAetherFrontierCharacter> Self=this;
    if(ResourceGate->IsEnabled()&&ResourceGate->Defer([Self]{if(Self.IsValid())Self->RefreshDerivedHealth();},EAetherEffectEventKind::ProjectionRefresh))return;
    FString Reason;PS->ApplyResolvedAttributes(*P,PS->GetNativeSkillGrants(),Reason);
    ForceNetUpdate();
}
void AAetherFrontierCharacter::BeginPlay()
{
    BindPersistentAbilities(); Super::BeginPlay();
    GetWorld()->GetSubsystem<UAetherNearbyRegistry>()->Register(this);
    Equipment->bProfileManaged=ProfileState()!=nullptr;
    if(HasAuthority())AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetPostureAttribute(),100);
    Equipment->OwnsItem.BindLambda([this](FName Id)
    {
        auto* PS=ProfileState();if(!PS)return true;
        if(const auto* P=PS->GetNativeProfile())
        {
            const auto& Definitions=FAetherV10Definitions::Get().Items;
            for(const auto& Item:P->Inventory.Items)if(const auto* D=Definitions.Items.Find(Item.DefinitionId))
                if(FName(*(D->EquipmentId.IsEmpty()?D->Id:D->EquipmentId))==Id)return true;
            return false;
        }
        return AetherInventory::OwnsEquipment(PS->Profile,Id,FAetherRules::Get());
    });
    Equipment->CanAct.BindLambda([this](){return Ready()&&!Carried&&!ReviveTarget&&!bPanel;});
    if (HasAuthority() && ProfileState()) ApplyProfileEquipment();
}
void AAetherFrontierCharacter::PossessedBy(AController* C)
{
    Super::PossessedBy(C); BindPersistentAbilities();
    if(HasAuthority()&&CompanionDecision)CompanionDecision->BeginCompanionControl();
    if(HasAuthority()&&ProfileState()&&ProfileState()->bNativeSkillsEnabled)ResourceGate->BlockForInitialLoad();
    if(HasAuthority() && ProfileState())
    { Equipment->bProfileManaged=true;GrantSpells();if(HasActorBegunPlay())ApplyProfileEquipment(); }
    // 本地权威服不会收到 OnRep_PlayerState；PossessedBy 完成后同样发布上下文就绪事件。
    OnPresentationChanged.Broadcast();
}
void AAetherFrontierCharacter::UnPossessed()
{if(CompanionDecision)CompanionDecision->EndCompanionControl();ReleaseHeldInput();CloseTrade();Super::UnPossessed();}
void AAetherFrontierCharacter::OnRep_PlayerState()
{ Super::OnRep_PlayerState(); BindPersistentAbilities(); OnPresentationChanged.Broadcast(); }
bool AAetherFrontierCharacter::SpellUnlocked(int32 Spell) const
{
    if((SkillAuthority==EAetherSkillAuthority::Profile))
    {
        const auto* State=NativeSkillView();const auto* Id=State?State->Hotbar.Find(Spell):nullptr;
        return Id&&SkillUnlocked(*Id);
    }
    return SkillAuthority==EAetherSkillAuthority::Definition&&Super::SpellUnlocked(Spell);
}
void AAetherFrontierCharacter::ApplyProfileEquipment()
{
    auto* PS=ProfileState(); if (!HasAuthority()||!PS||(SkillAuthority==EAetherSkillAuthority::Profile)) return;
    TArray<FAetherEquippedSlot> Slots;
    if(!AetherInventory::BuildLoadout(PS->Profile,FAetherRules::Get(),Slots))return;
    Equipment->RestoreLoadout(Slots);
}
void AAetherFrontierCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AAetherFrontierCharacter,bSprinting);DOREPLIFETIME(AAetherFrontierCharacter,bTravelPending);
    DOREPLIFETIME_CONDITION(AAetherFrontierCharacter,TravelWaitReason,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AAetherFrontierCharacter,RecoveryLife,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AAetherFrontierCharacter,RecoveryReason,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AAetherFrontierCharacter,RecoveryWait,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AAetherFrontierCharacter,bRecoveryAvailable,COND_OwnerOnly);
    DOREPLIFETIME(AAetherFrontierCharacter,Carried);DOREPLIFETIME(AAetherFrontierCharacter,CompanionOwner);
    DOREPLIFETIME(AAetherFrontierCharacter,CompanionId);DOREPLIFETIME(AAetherFrontierCharacter,bCompanionHold);DOREPLIFETIME(AAetherFrontierCharacter,EncounterId);DOREPLIFETIME(AAetherFrontierCharacter,BossPhase);DOREPLIFETIME(AAetherFrontierCharacter,BossVersion);DOREPLIFETIME(AAetherFrontierCharacter,BossPhaseStarted);DOREPLIFETIME(AAetherFrontierCharacter,BossPressure);DOREPLIFETIME(AAetherFrontierCharacter,bHealer);DOREPLIFETIME(AAetherFrontierCharacter,ReviveTarget);
}
void AAetherFrontierCharacter::Forward(float V){if(Alive()&&!bPanel&&!bTravelPending)AddMovementInput(FRotationMatrix(FRotator(0,GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::X),V);}
void AAetherFrontierCharacter::Right(float V){if(Alive()&&!bPanel&&!bTravelPending)AddMovementInput(FRotationMatrix(FRotator(0,GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y),V);}
void AAetherFrontierCharacter::Yaw(float V){if(!bPanel)AddControllerYawInput(V*GetDefault<UAetherPlayerPreferences>()->MouseSensitivity);}
void AAetherFrontierCharacter::Pitch(float V){const auto* P=GetDefault<UAetherPlayerPreferences>();if(!bPanel)AddControllerPitchInput((P->bInvertLook?V:-V)*P->MouseSensitivity);}
void AAetherFrontierCharacter::PressAttack()
{
    if(!FAetherControlledActionCatalog::Get().bValid||bAttackHeld||bPanel||bTravelPending||!Alive()||CombatTime()<StunUntil||Carried||ReviveTarget||AttackInputSequence==MAX_uint32)return;
    ++AttackInputSequence;
    bBufferedAttack=false; // 新按下替换上一条尚未提交的短缓冲。
    bAttackHeld=true;bAttackCharged=false;PressedAt=GetWorld()->GetTimeSeconds();
    Feedback=TEXT("攻击准备 · 松开轻击，按住蓄力");OnPresentationChanged.Broadcast();
    UE_LOG(LogTemp,Verbose,TEXT("AETHER_ATTACK_PRESSED input=%u synchronized_time=%.3f"),AttackInputSequence,CombatTime());
}
void AAetherFrontierCharacter::ReleaseAttack()
{
    const bool Attack=bAttackHeld,Heavy=GetWorld()->GetTimeSeconds()-PressedAt>=FAetherControlledActionCatalog::Get().AttackCharge;
    bAttackHeld=false;bAttackCharged=false;
    if(!FAetherControlledActionCatalog::Get().bValid||!Attack||bPanel||bTravelPending||!Alive()||CombatTime()<StunUntil||Carried||ReviveTarget){CancelAttackInput();return;}
    UE_LOG(LogTemp,Verbose,TEXT("AETHER_ATTACK_RELEASED input=%u synchronized_time=%.3f heavy=%d"),AttackInputSequence,CombatTime(),Heavy);
    if(Ready()){bBufferedAttack=false;ServerMeleeInput(Heavy,AttackInputSequence);}
    else {bBufferedAttack=true;bBufferedHeavy=Heavy;BufferedAttackSequence=AttackInputSequence;BufferedAttackUntil=CombatTime()+FAetherControlledActionCatalog::Get().AttackBuffer;}
}
void AAetherFrontierCharacter::ServerMeleeInput_Implementation(bool Heavy,uint32 Sequence)
{
    if(!FAetherControlledActionCatalog::Get().bValid||!Sequence||Sequence<=LastAttackInputSequence)return;LastAttackInputSequence=Sequence;
    if(!RequestMelee(Heavy?TEXT("Heavy"):TEXT("Light")))return;
    Equipment->Attack.InputSequence=Sequence;ForceNetUpdate();
    UE_LOG(LogTemp,Verbose,TEXT("AETHER_ATTACK_ACCEPTED input=%u attack=%u server_time=%.3f"),Sequence,Equipment->Attack.Serial,CombatTime());
}
void AAetherFrontierCharacter::SetupPlayerInputComponent(UInputComponent* I){PlayerInput->Setup(I);}
FKey AAetherFrontierCharacter::BindingFor(FName Name) const{return PlayerInput->BindingFor(Name);}
void AAetherFrontierCharacter::AetherBind(FName Name,FKey Key){PlayerInput->SetBinding(Name,Key);}
const TMap<FName,FKey>& AAetherFrontierCharacter::InputDefaults() const{return PlayerInput->Defaults();}
bool AAetherFrontierCharacter::HasGameplayBindings() const{return PlayerInput->HasBindings();}
void AAetherFrontierCharacter::ToggleLock()
{
    if(bPanel)return;if(LockedTarget){LockedTarget=nullptr;return;}
    double Best=FMath::Square(1500.);
    for(TActorIterator<AAetherCharacter> It(GetWorld());It;++It)if(It->Fighter!=EAetherFighter::Player&&It->Alive())
    {double D=FVector::DistSquared(It->GetActorLocation(),GetActorLocation());if(D<Best){FCollisionQueryParams Q(SCENE_QUERY_STAT(Lock),false,this);Q.AddIgnoredActor(*It);if(!GetWorld()->LineTraceTestByChannel(GetActorLocation(),It->GetActorLocation(),ECC_Visibility,Q)){Best=D;LockedTarget=*It;}}}
}
bool AAetherFrontierCharacter::CanStartLocomotion() const
{return Ready()&&!Carried&&!ReviveTarget&&!bTravelPending&&Stamina()>0;}
bool AAetherFrontierCharacter::CanJumpInternal_Implementation() const
{return CanStartLocomotion()&&Super::CanJumpInternal_Implementation();}
void AAetherFrontierCharacter::StartJumpInput()
{
    if(bPanel||!CanStartLocomotion())return;
    auto* Move=CastChecked<UAetherCharacterMovement>(GetCharacterMovement());
    if(Move->TryStand()&&Move->IsMovingOnGround())
    {
        bCrouchHeld=bCrouchToggled=false;
        if(AbilitySystem->TryActivateAbilityByClass(UAetherVaultAbility::StaticClass()))return;
        Jump();
    }
}
void AAetherFrontierCharacter::SetCrouchInput(bool Pressed)
{
    if(Pressed&&(bPanel||!CanStartLocomotion()||!GetCharacterMovement()->IsMovingOnGround()))return;
    bCrouchHeld=Pressed;ApplyCrouchIntent();
}
void AAetherFrontierCharacter::ToggleCrouchInput()
{
    if(bPanel||!CanStartLocomotion()||!GetCharacterMovement()->IsMovingOnGround())return;
    bCrouchToggled=!bCrouchToggled;ApplyCrouchIntent();
}
bool AAetherFrontierCharacter::UtilityModifierHeld() const
{
    const auto* PC=Cast<APlayerController>(Controller);return PC&&PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder);
}
void AAetherFrontierCharacter::SelectSpellInput(int32 Slot)
{if(!bPanel&&Alive()&&!UtilityModifierHeld())SelectedSpell=Slot;}
void AAetherFrontierCharacter::ApplyCrouchIntent()
{
    if(bCrouchHeld||bCrouchToggled)
    {
        if(bPanel||!CanStartLocomotion()||!GetCharacterMovement()->IsMovingOnGround())return;
        SetSprintInput(false);StopJumping();Crouch();
    }
    else UnCrouch();
}
void AAetherFrontierCharacter::SetSprintInput(bool Pressed)
{
    auto* Move=CastChecked<UAetherCharacterMovement>(GetCharacterMovement());
    Move->bWantsSprint=Pressed&&!bPanel&&CanStartLocomotion();
    if(Move->bWantsSprint&&Move->TryStand())bCrouchHeld=bCrouchToggled=false;
    bSprinting=Move->bWantsSprint&&Move->CanSprint();
}
void AAetherFrontierCharacter::ServerSprint_Implementation(bool Enabled)
{
    // 兼容既有服务的停止入口；正式保持输入通过 SavedMove 压缩标记传输。
    SetSprintInput(Enabled);
}
void AAetherFrontierCharacter::ReleaseHeldInput()
{
    bCrouchHeld=bCrouchToggled=false;
    StopJumping();SetSprintInput(false);SetCrouchInput(false);CancelAttackInput();ServerBlock(false);
}
void AAetherFrontierCharacter::OnStartCrouch(float H,float Scaled)
{
    Super::OnStartCrouch(H,Scaled);if(IsLocallyControlled())CrouchCameraOffset+=Scaled;
}
void AAetherFrontierCharacter::OnEndCrouch(float H,float Scaled)
{
    Super::OnEndCrouch(H,Scaled);if(IsLocallyControlled())CrouchCameraOffset-=Scaled;
}
void AAetherFrontierCharacter::Notify_Implementation(const FString& Message){Feedback=Message;OnPresentationChanged.Broadcast();}
void AAetherFrontierCharacter::ReleaseCarry()
{if(WorldActions)WorldActions->Release();}
void AAetherFrontierCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    if(CompanionDecision)CompanionDecision->EndCompanionControl();
    if(DerivedAttributeSystem.IsValid())DerivedAttributeSystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetGearMaxHealthAttribute()).Remove(DerivedHealthDelegate);
    DerivedAttributeSystem.Reset();DerivedHealthDelegate.Reset();
    TradeSession={};SaleConfirmation={};PendingTradeAuthorization.Invalidate();
    InteractionFocus={};bHasInteractionFocus=false;
    if(auto* Registry=GetWorld()->GetSubsystem<UAetherNearbyRegistry>())Registry->Unregister(this);
    if(HasAuthority())if(auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>())Mode->ReleaseNativePawn(this);
    ClearTravelSource();ReleaseCarry();Super::EndPlay(Reason);
}
void AAetherFrontierCharacter::ServerAction_Implementation(FName Action,int32 Index)
{
    if(Action=="Recover"){ServerRecover_Implementation(RecoveryLife);return;}
    if(ResourceGate->IsBlocked()){if(Action=="Save")Notify(TEXT("保存暂忙：角色事务尚未确认，请稍后按 F5 重试。"));return;} // 新操作可拒绝，已经接受的资源动作由屏障保留。
    auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>(); auto* PS=ProfileState(); if(!Mode||!PS)return;
    if(bTravelPending&&Action!="Save")return;
    if(CombatTime()<NextServerAction)return;NextServerAction=CombatTime()+.12f;
    if(Action=="Weather")
    {
#if !UE_BUILD_SHIPPING
        if(GetNetMode()==NM_Standalone||(Controller&&Controller->IsLocalController()))
        {auto* S=GetWorld()->GetGameState<AAetherFrontierState>();S->bRain=!S->bRain;Notify(S->bRain?TEXT("Rain: thermal and visual channels only."):TEXT("Clear weather."));}
#endif
        return;
    }
    if(Action=="Save")
    {
        if(!Mode->IsNativeMode())Notify(Mode->SaveWorld()?TEXT("World and profiles saved."):TEXT("Save deferred: world has pending reactions."));
        else if(!Mode->NativeSceneReady())Notify(TEXT("世界尚未就绪，请稍后按 F5 重试。"));
        else if(ManualWorldSave.IsPending())Notify(TEXT("保存请求仍在确认中，请稍候。"));
        else if(auto* Persistence=GetGameInstance()?GetGameInstance()->GetSubsystem<UAetherNativePersistence>():nullptr)
        {
            ManualWorldSave.Start([Persistence]{return Persistence->SaveLoadedPhysics();});
            Notify(TEXT("正在保存世界检查点，请等待确认。"));
        }
        else Notify(TEXT("保存服务不可用，请稍后重试。"));
        return;
    }
    if(!Alive())return;
    // 客户端必须提交所见目标；旧的无目标字符串入口不能重新选择邻近对象。
    if(Action=="Interact"){Notify(TEXT("请重新选择交互目标。"));return;}
    if(Mode->ExecutePartyAction(this,Action))return;
    if(Action=="Throw"||Action=="Carry"||Action=="Push"){Notify(TEXT("请重新选择场景物体。"));return;}
    if(Action=="Claim"&&Mode->IsNativeMode()){Notify(TEXT("请在任务日志中领取待领奖励。"));return;}
    if(Action=="Claim")
    {auto Next=PS->Profile;bool Changed=Next.CollectPending();Changed|=AetherQuests::Settle(Next,Mode->Database->WorldFacts,true);if(Changed)Notify(Mode->Commit(PS,Next)?TEXT("Pending rewards received."):TEXT("Reward save failed; retry."));return;}
    // Inventory mutations use ServerInventory with stable client-selected identity.

}
void AAetherFrontierCharacter::ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit)
{
    if(DeferEquipmentHit(Hit))return;
    if(Reactive->bOwnerOnlyStimuli&&Hit.Source!=GetOwner())return;
    if(auto* Other=Cast<AAetherCharacter>(Hit.Source);Other&&Other->Reactive->bOwnerOnlyStimuli&&Other->GetOwner()!=this)return;
    const bool Blocked=CombatRuntime->DeferredDefense.IsSet()?CombatRuntime->DeferredDefense->bBlocked:
        bBlocking&&Equipment->GuardDefinition()&&Hit.Source&&FVector::DotProduct(GetActorForwardVector(),(Hit.Source->GetActorLocation()-GetActorLocation()).GetSafeNormal())>.25;
    if(const auto* Source=::Cast<AAetherCharacter>(Hit.Source); Source && Source->Fighter==EAetherFighter::Player && Fighter==EAetherFighter::Player)return;
    Super::ReceiveEquipmentHit_Implementation(Hit);
    if(auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>())
    { M->CreditHit(this,Cast<AAetherCharacter>(Hit.Source)); if(Blocked)M->Observe(this,"Block"); }
}
void AAetherFrontierCharacter::Tick(float Dt)
{
    Super::Tick(Dt);
    if(HasAuthority())if(const auto Message=ManualWorldSave.Poll();Message.IsSet())Notify(Message.GetValue());
    UpdateRecoveryState();
    if(bPanel||bTravelPending||!Alive()||CombatTime()<StunUntil||Carried||ReviveTarget)CancelAttackInput();
    if(IsLocallyControlled())
    {
        if(bAttackHeld&&!bAttackCharged&&GetWorld()->GetTimeSeconds()-PressedAt>=FAetherControlledActionCatalog::Get().AttackCharge)
        {bAttackCharged=true;Feedback=TEXT("重击就绪 · 松开执行");OnPresentationChanged.Broadcast();}
        if(bBufferedAttack)
        {
            if(CombatTime()>BufferedAttackUntil)CancelAttackInput();
            else if(Ready()){const bool Heavy=bBufferedHeavy;bBufferedAttack=false;ServerMeleeInput(Heavy,BufferedAttackSequence);}
        }
    }
    MaintainTrade();
    UpdateSafeTravel();
    CheckClosureClient(Dt);
    if(IsLocallyControlled()&&LockedTarget)
    {
        if(!IsValid(LockedTarget)||!LockedTarget->Alive()||FVector::DistSquared(GetActorLocation(),LockedTarget->GetActorLocation())>FMath::Square(1800.))LockedTarget=nullptr;
        else if(!bPanel&&Controller)Controller->SetControlRotation(FMath::RInterpTo(GetControlRotation(),(LockedTarget->GetActorLocation()-GetActorLocation()).Rotation(),Dt,7));
    }
    if(IsLocallyControlled())
    {
        CrouchCameraOffset=FMath::FInterpTo(CrouchCameraOffset,0.f,Dt,12.f);
        Arm->TargetOffset.Z=CrouchCameraOffset;
    }
    const auto* Movement=CastChecked<UAetherCharacterMovement>(GetCharacterMovement());
    if(HasAuthority()&&bSprinting&&Movement->CanSprint()&&GetVelocity().SizeSquared2D()>1)
        AbilitySystem->ApplyModToAttribute(UAetherAttributes::GetStaminaAttribute(),EGameplayModOp::Additive,-32*Dt);
    if(!HasAuthority() && IsLocallyControlled() && FParse::Param(FCommandLine::Get(),TEXT("AetherV4NetClient")))
    {
        static float TestTime=0;static float RequestTime=0;TestTime+=Dt;
        auto* PS=ProfileState();auto* S=GetWorld()->GetGameState<AAetherFrontierState>();
        if(PS && TestTime-RequestTime>.5f){RefreshInteractionFocus();InteractV4();RequestTime=TestTime;}
        if(PS&&S&&TestTime>4&&PS->Profile.Evidence.Contains("SupplyA")&&PS->Profile.Count("Supply")==1&&S->bSupplyRestored)
        {
            const bool DataFixture=FParse::Param(FCommandLine::Get(),TEXT("AetherV802Net"));
            const auto* Weapon=Equipment->InSlot("MainHand");
            const bool EquipmentValid=DataFixture?PS->Profile.Count("SurveySword")==1&&PS->Profile.Equipped.Contains("MainHand")&&Weapon&&Weapon->ItemId=="TrainingSword"&&Equipment->VisualForSlot("MainHand"):Equipment->Slots.IsEmpty();
            bool Pass=PS->Profile.Claims.IsEmpty()&&!PS->Profile.Evidence.Contains("SupplyRestored")&&AbilitySystem==PS->AbilitySystem&&AbilitySystem->GetOwnerActor()==PS
                &&EquipmentValid&&!SpellUnlocked(0)&&GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation()->GetStats().Registered==0;
            UE_LOG(LogTemp,Display,TEXT("AETHER_V4_NET_%s profile=%s revision=%d isolated=%d claims=%d evidence=%d asc=%d owner=%d slots=%d locked=%d bodies=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),*PS->Profile.CharacterId,PS->Profile.Revision,Pass,PS->Profile.Claims.Num(),PS->Profile.Evidence.Contains("SupplyRestored"),AbilitySystem==PS->AbilitySystem,AbilitySystem->GetOwnerActor()==PS,Equipment->Slots.Num(),!SpellUnlocked(0),GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation()->GetStats().Registered);
            if(DataFixture)UE_LOG(LogTemp,Display,TEXT("AETHER_V802_EQUIPMENT_%s profile=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),*PS->Profile.CharacterId);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        }
        if(TestTime>30){UE_LOG(LogTemp,Display,TEXT("AETHER_V4_NET_FAIL timeout"));FPlatformMisc::RequestExitWithStatus(false,1);}
    }
    if(!HasAuthority())return;
    // Native profiles publish derived limits from committed progression/equipment only.
    if(!(SkillAuthority==EAetherSkillAuthority::Profile))if(auto* PS=ProfileState())MaxHealth=100+5*FMath::Clamp(PS->Profile.Experience/200,0,4);
    if(ReviveTarget)
    {
        if(!IsValid(ReviveTarget)||!Alive()||ReviveTarget->Alive()||CombatRuntime->DamageReceivedCount!=ReviveDamageSerial||CombatTime()<StunUntil||FVector::DistSquared(GetActorLocation(),ReviveTarget->GetActorLocation())>FMath::Square(220.0))ReviveTarget=nullptr;
        if(ReviveTarget){FCollisionQueryParams Q(SCENE_QUERY_STAT(RescueLOS),false,this);Q.AddIgnoredActor(ReviveTarget);if(GetWorld()->LineTraceTestByChannel(GetActorLocation(),ReviveTarget->GetActorLocation(),ECC_Visibility,Q))ReviveTarget=nullptr;}
        if(!ReviveTarget)if(auto* Spec=AbilitySystem->FindAbilitySpecFromClass(UAetherReviveAbility::StaticClass()))AbilitySystem->CancelAbilityHandle(Spec->Handle);
    }
}

bool AAetherFrontierCharacter::ValidateCastCommit(const FAetherCastExecution& Execution,const AAetherCharacter* ActualTarget)
{return !CompanionDecision||CompanionDecision->ValidateSkillCommit(Execution,ActualTarget);}
float AAetherFrontierCharacter::TakeDamage(float Amount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer)
{
    if(DeferDamage(Amount,Event,EventInstigator,Causer))return 0;
    auto* SourceCharacter=Cast<AAetherFrontierCharacter>(Causer);
    if(Reactive->bOwnerOnlyStimuli&&Causer!=GetOwner()&&(!EventInstigator||EventInstigator->GetPawn()!=GetOwner()))return 0;
    if(SourceCharacter&&SourceCharacter->Reactive->bOwnerOnlyStimuli&&SourceCharacter->GetOwner()!=this)return 0;
    if(!SourceCharacter&&EventInstigator)SourceCharacter=Cast<AAetherFrontierCharacter>(EventInstigator->GetPawn());
    if(!EncounterId.IsNone()&&Fighter!=EAetherFighter::Player&&SourceCharacter&&SourceCharacter->Fighter==EAetherFighter::Player)
        if(auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();M&&M->Encounters&&!M->Encounters->Participates(SourceCharacter,EncounterId))return 0;
    if(Fighter==EAetherFighter::BellKnight&&BossPhase!=2)Amount*=.65f;
    const float Applied=Super::TakeDamage(Amount,Event,EventInstigator,Causer);
    if(Applied>0)
    {
        auto* Source=Cast<AAetherCharacter>(Causer);if(!Source&&EventInstigator)Source=Cast<AAetherCharacter>(EventInstigator->GetPawn());
        if(auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>())Mode->CreditHit(this,Source);
        if(Fighter==EAetherFighter::BellKnight)BossPressure+=Applied*.25f;
    }
    return Applied;
}

void AAetherFrontierCharacter::ClaimRewards()
{
 if(!bPanel||Panel!=2)return;
 if((SkillAuthority==EAetherSkillAuthority::Profile)){FString Why;AetherNativeInventory::Shortcut(*this,"Claim",NAME_None,Why);Feedback=Why;OnPresentationChanged.Broadcast();}
 else ServerAction("Claim");
}
void AAetherFrontierCharacter::CycleItem()
{
 if(!bPanel||!ProfileState())return;
 if(Panel==1&&(SkillAuthority==EAetherSkillAuthority::Profile)){FString Why;AetherNativeInventory::Shortcut(*this,"Next",NAME_None,Why);OnPresentationChanged.Broadcast();return;}
 if(Panel==1&&!ProfileState()->Profile.Inventory.IsEmpty()){SelectedItem=(SelectedItem+1)%ProfileState()->Profile.Inventory.Num();SelectedInstance=ProfileState()->Profile.Inventory[SelectedItem].InstanceId;}
 if(Panel==2)if(const auto* PC=Cast<APlayerController>(GetController()))if(auto* LP=PC->GetLocalPlayer())
 {
  const auto& Snapshot=LP->GetSubsystem<UAetherCommandClient>()->GetProfile();
  if(Snapshot.IsSet())TrackedQuest=AetherGuide::SelectQuest(Snapshot.GetValue(),TrackedQuest,true);
 }
 OnPresentationChanged.Broadcast();
}

bool AAetherFrontierCharacter::AllowsGeneratedMotion() const
{
    // 场景交互占用由玩法所有者明确提供，动作插件不反查任务或持久化。
    return Super::AllowsGeneratedMotion()&&!Carried&&!ReviveTarget&&!bTravelPending;
}

