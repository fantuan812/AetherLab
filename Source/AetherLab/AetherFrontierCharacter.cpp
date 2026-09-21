#include "Characters/AetherFrontierCharacter.h"
#include "AetherGuide.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "Interaction/AetherWorldActionComponent.h"
#include "AetherFrontier.h"
#include "Inventory/AetherNativeInventory.h"
#include "Inventory/AetherResourceGate.h"
#include "AetherContent.h"
#include "Definitions/AetherV10Definitions.h"
#include "AetherRules.h"
#include "AetherInventoryRules.h"
#include "AetherActions.h"
#include "AetherInputProfile.h"
#include "Presentation/AetherPlayerPreferences.h"
#include "Movement/AetherCharacterMovement.h"
#include "Movement/AetherVaultAbility.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
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

AAetherFrontierCharacter::AAetherFrontierCharacter(const FObjectInitializer& ObjectInitializer)
    :Super(ObjectInitializer.SetDefaultSubobjectClass<UAetherCharacterMovement>(ACharacter::CharacterMovementComponentName))
{ bUseBasicAssets=true;JumpMaxCount=1;JumpMaxHoldTime=.18f;CarryHandle=CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("CarryHandle"));WorldActions=CreateDefaultSubobject<UAetherWorldActionComponent>(TEXT("WorldActions")); }
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
        if(HasAuthority())PS->RefreshTemporarySkills();
    }
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
    if(HasAuthority()&&ProfileState()&&ProfileState()->bNativeSkillsEnabled)ResourceGate->BlockForInitialLoad();
    if(HasAuthority() && ProfileState())
    { Equipment->bProfileManaged=true;GrantSpells();if(HasActorBegunPlay())ApplyProfileEquipment(); }
    // 本地权威服不会收到 OnRep_PlayerState；PossessedBy 完成后同样发布上下文就绪事件。
    OnPresentationChanged.Broadcast();
}
void AAetherFrontierCharacter::UnPossessed()
{ReleaseHeldInput();CloseTrade();Super::UnPossessed();}
void AAetherFrontierCharacter::OnRep_PlayerState()
{ Super::OnRep_PlayerState(); BindPersistentAbilities(); OnPresentationChanged.Broadcast(); }
bool AAetherFrontierCharacter::SpellUnlocked(int32 Spell) const
{
    if(UsesNativeSkills())
    {
        const auto* State=NativeSkillView();const auto* Id=State?State->Hotbar.Find(Spell):nullptr;
        return Id&&SkillUnlocked(*Id);
    }
    const auto* PS=ProfileState();return Spell>=0&&Spell<4&&(!PS||(PS->Profile.LearnedSpells&(1<<Spell))!=0);
}
void AAetherFrontierCharacter::ApplyProfileEquipment()
{
    auto* PS=ProfileState(); if (!HasAuthority()||!PS||UsesNativeSkills()) return;
    TArray<FAetherEquippedSlot> Slots;
    if(!AetherInventory::BuildLoadout(PS->Profile,FAetherRules::Get(),Slots))return;
    Equipment->RestoreLoadout(Slots);
}
void AAetherFrontierCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AAetherFrontierCharacter,bSprinting);DOREPLIFETIME(AAetherFrontierCharacter,bTravelPending);
    DOREPLIFETIME(AAetherFrontierCharacter,Carried);DOREPLIFETIME(AAetherFrontierCharacter,CompanionOwner);
    DOREPLIFETIME(AAetherFrontierCharacter,CompanionId);DOREPLIFETIME(AAetherFrontierCharacter,bCompanionHold);DOREPLIFETIME(AAetherFrontierCharacter,EncounterId);DOREPLIFETIME(AAetherFrontierCharacter,BossPhase);DOREPLIFETIME(AAetherFrontierCharacter,BossVersion);DOREPLIFETIME(AAetherFrontierCharacter,BossPhaseStarted);DOREPLIFETIME(AAetherFrontierCharacter,BossPressure);DOREPLIFETIME(AAetherFrontierCharacter,bHealer);DOREPLIFETIME(AAetherFrontierCharacter,ReviveTarget);
}
void AAetherFrontierCharacter::Forward(float V){if(Alive()&&!bPanel&&!bTravelPending)AddMovementInput(FRotationMatrix(FRotator(0,GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::X),V);}
void AAetherFrontierCharacter::Right(float V){if(Alive()&&!bPanel&&!bTravelPending)AddMovementInput(FRotationMatrix(FRotator(0,GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y),V);}
void AAetherFrontierCharacter::Yaw(float V){if(!bPanel)AddControllerYawInput(V*GetDefault<UAetherPlayerPreferences>()->MouseSensitivity);}
void AAetherFrontierCharacter::Pitch(float V){const auto* P=GetDefault<UAetherPlayerPreferences>();if(!bPanel)AddControllerPitchInput((P->bInvertLook?V:-V)*P->MouseSensitivity);}
void AAetherFrontierCharacter::PressAttack(){bAttackHeld=!bPanel;if(bAttackHeld)PressedAt=GetWorld()->GetTimeSeconds();}
void AAetherFrontierCharacter::ReleaseAttack(){const bool Attack=bAttackHeld;bAttackHeld=false;if(Attack&&!bPanel)ServerAttack(GetWorld()->GetTimeSeconds()-PressedAt>=.35f);}
void AAetherFrontierCharacter::SetupPlayerInputComponent(UInputComponent* I)
{
    auto* Enhanced=Cast<UEnhancedInputComponent>(I);auto* PC=Cast<APlayerController>(Controller);
    auto* Sub=PC&&PC->GetLocalPlayer()?ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()):nullptr;
    if(!Enhanced||!Sub)return;
    if(GameplayContext)Sub->RemoveMappingContext(GameplayContext);
    auto* Template=LoadObject<UInputMappingContext>(nullptr,TEXT("/Game/AetherCore/Input/IMC_Gameplay.IMC_Gameplay"));
    GameplayContext=Template?DuplicateObject<UInputMappingContext>(Template,this):NewObject<UInputMappingContext>(this);InputActions.Reset();DefaultBindings.Reset();
    auto Action=[&](FName Name,FKey Key,EInputActionValueType Type)
    {
        if(auto* Existing=InputActions.Find(Name))return Existing->Get();
        DefaultBindings.Add(Name,Key);
        const FString Path=TEXT("/Game/AetherCore/Input/IA_")+Name.ToString()+TEXT(".IA_")+Name.ToString();
        auto* A=LoadObject<UInputAction>(nullptr,*Path);
        if(!A){A=NewObject<UInputAction>(this);A->ValueType=Type;A->bConsumeInput=false;}
        InputActions.Add(Name,A);
        const auto* Override=GetDefault<UAetherInputProfile>()->Keys.Find(Name);
        if(Override)GameplayContext->UnmapKey(A,Key);
        const FKey Desired=Override?*Override:Key;
        if(!GameplayContext->GetMappings().ContainsByPredicate([&](const auto& M){return M.Action==A&&M.Key==Desired;}))GameplayContext->MapKey(A,Desired);
        return A;
    };
    auto Bind=[&](FName Name,FKey Key,ETriggerEvent Event,void(AAetherFrontierCharacter::*Fn)())
    {auto* A=Action(Name,Key,EInputActionValueType::Boolean);Enhanced->BindAction(A,Event,this,Fn);if(Event==ETriggerEvent::Completed)Enhanced->BindAction(A,ETriggerEvent::Canceled,this,Fn);};
    Bind("Tab",EKeys::Tab,ETriggerEvent::Started,&AAetherFrontierCharacter::CycleItem);
    Bind("Delete",EKeys::Delete,ETriggerEvent::Started,&AAetherFrontierCharacter::SellItem);
    Bind("Seven",EKeys::Seven,ETriggerEvent::Started,&AAetherFrontierCharacter::BuyMana);
    Bind("Eight",EKeys::Eight,ETriggerEvent::Started,&AAetherFrontierCharacter::BuyRation);
    Bind("Z",EKeys::Z,ETriggerEvent::Started,&AAetherFrontierCharacter::UseMana);
    Bind("F8",EKeys::F8,ETriggerEvent::Started,&AAetherFrontierCharacter::Recover);
    Bind("H",EKeys::H,ETriggerEvent::Started,&AAetherFrontierCharacter::PartyCommand);
    Bind("Delete",EKeys::Delete,ETriggerEvent::Started,&AAetherFrontierCharacter::Dismiss);
    Bind("Attack",EKeys::LeftMouseButton,ETriggerEvent::Started,&AAetherFrontierCharacter::PressAttack);
    Bind("Attack",EKeys::LeftMouseButton,ETriggerEvent::Completed,&AAetherFrontierCharacter::ReleaseAttack);
    Bind("Guard",EKeys::RightMouseButton,ETriggerEvent::Started,&AAetherFrontierCharacter::GuardOn);
    Bind("Guard",EKeys::RightMouseButton,ETriggerEvent::Completed,&AAetherFrontierCharacter::GuardOff);
    Bind("Sprint",EKeys::LeftShift,ETriggerEvent::Started,&AAetherFrontierCharacter::SprintOn);
    Bind("Sprint",EKeys::LeftShift,ETriggerEvent::Completed,&AAetherFrontierCharacter::SprintOff);
    Bind("Dodge",EKeys::LeftAlt,ETriggerEvent::Started,&AAetherFrontierCharacter::Dodge);
    Bind("Jump",EKeys::SpaceBar,ETriggerEvent::Started,&AAetherFrontierCharacter::JumpV4);
    Bind("Jump",EKeys::SpaceBar,ETriggerEvent::Completed,&AAetherFrontierCharacter::StopJumping);
    Bind("Crouch",EKeys::LeftControl,ETriggerEvent::Started,&AAetherFrontierCharacter::CrouchOn);
    Bind("Crouch",EKeys::LeftControl,ETriggerEvent::Completed,&AAetherFrontierCharacter::CrouchOff);
    Bind("One",EKeys::One,ETriggerEvent::Started,&AAetherFrontierCharacter::Spell0);
    Bind("Two",EKeys::Two,ETriggerEvent::Started,&AAetherFrontierCharacter::Spell1);
    Bind("Three",EKeys::Three,ETriggerEvent::Started,&AAetherFrontierCharacter::Spell2);
    Bind("Four",EKeys::Four,ETriggerEvent::Started,&AAetherFrontierCharacter::Spell3);
    Bind("Cast",EKeys::MiddleMouseButton,ETriggerEvent::Started,&AAetherFrontierCharacter::CastSelectedV4);
    Bind("Interact",EKeys::E,ETriggerEvent::Started,&AAetherFrontierCharacter::InteractV4);
    Bind("Potion",EKeys::Q,ETriggerEvent::Started,&AAetherFrontierCharacter::UsePotion);
    Bind("Carry",EKeys::G,ETriggerEvent::Started,&AAetherFrontierCharacter::Carry);
    Bind("Push",EKeys::V,ETriggerEvent::Started,&AAetherFrontierCharacter::Push);
    Bind("R",EKeys::R,ETriggerEvent::Started,&AAetherFrontierCharacter::EquipNext);
    Bind("T",EKeys::T,ETriggerEvent::Started,&AAetherFrontierCharacter::Shield);
    Bind("F5",EKeys::F5,ETriggerEvent::Started,&AAetherFrontierCharacter::SaveV4);
    Bind("B",EKeys::B,ETriggerEvent::Started,&AAetherFrontierCharacter::SplitStack);
    Bind("N",EKeys::N,ETriggerEvent::Started,&AAetherFrontierCharacter::MergeStacks);
    Bind("I",EKeys::I,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleInventory);
    Bind("J",EKeys::J,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleQuests);
    Bind("K",EKeys::K,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleSkills);
    Bind("M",EKeys::M,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleMap);
    Bind("P",EKeys::P,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleParty);
    Bind("Escape",EKeys::Escape,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleMenu);
    Bind("Debug",EKeys::F10,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleDebug);
    Bind("Weather",EKeys::F11,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleWeather);
    Bind("Throw",EKeys::C,ETriggerEvent::Started,&AAetherFrontierCharacter::Throw);
    Bind("Claim",EKeys::Enter,ETriggerEvent::Started,&AAetherFrontierCharacter::ClaimRewards);
    Bind("Invite",EKeys::Y,ETriggerEvent::Started,&AAetherFrontierCharacter::Invite);
    Bind("AcceptInvite",EKeys::U,ETriggerEvent::Started,&AAetherFrontierCharacter::AcceptInvite);
    Bind("LeaveParty",EKeys::O,ETriggerEvent::Started,&AAetherFrontierCharacter::LeaveParty);
    Bind("Lock",EKeys::F,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleLock);
    for(auto Pair:{TPair<FName,FKey>("Forward",EKeys::W),TPair<FName,FKey>("Backward",EKeys::S),TPair<FName,FKey>("Left",EKeys::A),TPair<FName,FKey>("Right",EKeys::D)})
    {
        auto* A=Action(Pair.Key,Pair.Value,EInputActionValueType::Boolean);const FName Name=Pair.Key;
        Enhanced->BindActionValueLambda(A,ETriggerEvent::Triggered,[this,Name](const FInputActionValue&){if(Name=="Forward")Forward(1);else if(Name=="Backward")Forward(-1);else Right(Name=="Left"?-1:1);});
    }
    Enhanced->BindActionValueLambda(Action("LookX",EKeys::MouseX,EInputActionValueType::Axis1D),ETriggerEvent::Triggered,[this](const FInputActionValue& V){Yaw(V.Get<float>());});
    Enhanced->BindActionValueLambda(Action("LookY",EKeys::MouseY,EInputActionValueType::Axis1D),ETriggerEvent::Triggered,[this](const FInputActionValue& V){Pitch(V.Get<float>());});
    // 手柄视角按秒计算，不混用鼠标每帧位移。局部死区只应用于摇杆轴。
    auto PadAxis=[&](FName Name,FKey Key,TFunction<void(float)> Apply)
    {
        auto* A=Action(Name,Key,EInputActionValueType::Axis1D);
        GameplayContext->UnmapKey(A,Key);
        auto& CleanMapping=GameplayContext->MapKey(A,Key);
        auto* Dead=NewObject<UInputModifierDeadZone>(GameplayContext);Dead->LowerThreshold=.15f;Dead->UpperThreshold=1;
        CleanMapping.Modifiers.Add(Dead);
        Enhanced->BindActionValueLambda(A,ETriggerEvent::Triggered,[Apply=MoveTemp(Apply)](const FInputActionValue& V){Apply(V.Get<float>());});
    };
    PadAxis("PadMoveX",EKeys::Gamepad_LeftX,[this](float V){Right(V);});
    PadAxis("PadMoveY",EKeys::Gamepad_LeftY,[this](float V){Forward(V);});
    PadAxis("PadLookX",EKeys::Gamepad_RightX,[this](float V)
    {if(!bPanel)AddControllerYawInput(V*120.f*GetWorld()->GetDeltaSeconds()*GetDefault<UAetherPlayerPreferences>()->ControllerSensitivity);});
    PadAxis("PadLookY",EKeys::Gamepad_RightY,[this](float V)
    {const auto* P=GetDefault<UAetherPlayerPreferences>();if(!bPanel)AddControllerPitchInput(V*(P->bInvertLook?1.f:-1.f)*90.f*GetWorld()->GetDeltaSeconds()*P->ControllerSensitivity);});
    const TPair<FName,FKey> Pad[]={
        {"Attack",EKeys::Gamepad_RightTrigger},{"Guard",EKeys::Gamepad_LeftTrigger},
        {"Jump",EKeys::Gamepad_FaceButton_Bottom},{"Dodge",EKeys::Gamepad_FaceButton_Right},
        {"Interact",EKeys::Gamepad_FaceButton_Left},{"Potion",EKeys::Gamepad_FaceButton_Top},
        {"Sprint",EKeys::Gamepad_LeftThumbstick},{"Lock",EKeys::Gamepad_RightThumbstick},
        {"Crouch",EKeys::Gamepad_LeftShoulder},{"Cast",EKeys::Gamepad_RightShoulder},
        {"One",EKeys::Gamepad_DPad_Up},{"Two",EKeys::Gamepad_DPad_Right},
        {"Three",EKeys::Gamepad_DPad_Down},{"Four",EKeys::Gamepad_DPad_Left},
        {"I",EKeys::Gamepad_Special_Left},{"Escape",EKeys::Gamepad_Special_Right}};
    for(const auto& P:Pad)if(auto* A=InputActions.Find(P.Key))
        if(!GameplayContext->GetMappings().ContainsByPredicate([&](const auto& M){return M.Action==A->Get()&&M.Key==P.Value;}))
            GameplayContext->MapKey(A->Get(),P.Value);
    // 按住左肩时十字键成为物理交互，ChordBlocker 阻止底层技能选择穿透。
    const TPair<FName,FKey> Utility[]={{"Carry",EKeys::Gamepad_DPad_Up},{"Throw",EKeys::Gamepad_DPad_Right},
        {"Push",EKeys::Gamepad_DPad_Down},{"Z",EKeys::Gamepad_DPad_Left}};
    for(const auto& P:Utility)
    {
        GameplayContext->UnmapKey(InputActions[P.Key],P.Value);
        auto& M=GameplayContext->MapKey(InputActions[P.Key],P.Value);
        auto* Chord=NewObject<UInputTriggerChordAction>(GameplayContext);Chord->ChordAction=InputActions["Crouch"];M.Triggers.Add(Chord);
    }
    Sub->AddMappingContext(GameplayContext,0);
    OnPresentationChanged.Broadcast();
}
FKey AAetherFrontierCharacter::BindingFor(FName Name) const
{
 const auto* A=InputActions.Find(Name);if(A&&GameplayContext)for(const auto& M:GameplayContext->GetMappings())if(M.Action==A->Get()&&!M.Key.IsGamepadKey())return M.Key;return FKey();
}
void AAetherFrontierCharacter::AetherBind(FName Name,FKey Key)
{
 if(!IsLocallyControlled()||!GameplayContext||!Key.IsValid()||Key.IsGamepadKey()||Key.IsAxis1D()||Key.IsAxis2D()||Name=="LookX"||Name=="LookY"||Name=="Escape"||Key==EKeys::Escape)return;
 auto* A=InputActions.Find(Name);if(!A)return;const FKey Old=BindingFor(Name);if(Old==Key)return;
 ReleaseHeldInput();
 auto* Profile=GetMutableDefault<UAetherInputProfile>();
 // A single mapping per action: swap conflicts so no command is silently orphaned.
 TArray<FName> Conflicts;for(const auto& Pair:InputActions)if(Pair.Key!=Name&&BindingFor(Pair.Key)==Key&&(UAetherInputProfile::Context(Name)&UAetherInputProfile::Context(Pair.Key)))Conflicts.Add(Pair.Key);
 for(auto Other:Conflicts){auto* OtherAction=InputActions[Other].Get();GameplayContext->UnmapKey(OtherAction,Key);GameplayContext->MapKey(OtherAction,Old);Profile->Keys.Add(Other,Old);}
 GameplayContext->UnmapKey(A->Get(),Old);GameplayContext->MapKey(A->Get(),Key);Profile->Keys.Add(Name,Key);Profile->SaveConfig();
 Feedback=FString::Printf(TEXT("Bound %s: %s. Conflicting binding swapped."),*Name.ToString(),*Key.GetDisplayName().ToString());
 if(auto* PC=Cast<APlayerController>(Controller))if(auto* Sub=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))Sub->RequestRebuildControlMappings();
}
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
        if(AbilitySystem->TryActivateAbilityByClass(UAetherVaultAbility::StaticClass()))return;
        Jump();
    }
}
void AAetherFrontierCharacter::SetCrouchInput(bool Pressed)
{
    if(Pressed)
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
    if(Move->bWantsSprint)Move->TryStand();
    bSprinting=Move->bWantsSprint&&Move->CanSprint();
}
void AAetherFrontierCharacter::ServerSprint_Implementation(bool Enabled)
{
    // 兼容既有服务的停止入口；正式保持输入通过 SavedMove 压缩标记传输。
    SetSprintInput(Enabled);
}
void AAetherFrontierCharacter::ReleaseHeldInput()
{
    StopJumping();SetSprintInput(false);SetCrouchInput(false);bAttackHeld=false;ServerBlock(false);
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
    TradeSession={};SaleConfirmation={};PendingTradeAuthorization.Invalidate();
    InteractionFocus={};bHasInteractionFocus=false;
    if(auto* Registry=GetWorld()->GetSubsystem<UAetherNearbyRegistry>())Registry->Unregister(this);
    if(HasAuthority())if(auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>())Mode->ReleaseNativePawn(this);
    ClearTravelSource();ReleaseCarry();Super::EndPlay(Reason);
}
void AAetherFrontierCharacter::ServerAction_Implementation(FName Action,int32 Index)
{
    if(ResourceGate->IsBlocked())return; // 新操作可拒绝，已经接受的资源动作由屏障保留。
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
    if(Action=="Recover")
    {
        if(Mode->IsNativeMode()){Mode->RecoverNativePlayer(this);return;}
        if(Alive()||TimeSinceDamage()<3)return;
        ReleaseCarry();SetVitals(MaxHealth,100,100);ResetCombat();
        BeginSafeTravel(PS->Profile.bRegistered?FVector(-500,-500,120):FVector(-6500,-29000,120));return;
    }
    if(Action=="Save"){Notify(Mode->SaveWorld()?TEXT("World and profiles saved."):TEXT("Save deferred: world has pending reactions."));return;}
    if(!Alive())return;
    // 客户端必须提交所见目标；旧的无目标字符串入口不能重新选择邻近对象。
    if(Action=="Interact"){Notify(TEXT("请重新选择交互目标。"));return;}
    if(Mode->ExecutePartyAction(this,Action))return;
    if(Action=="Throw"||Action=="Carry"||Action=="Push"){WorldActions->Begin(Action);return;}
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
    const bool Blocked=bBlocking&&Equipment->GuardDefinition()&&Hit.Source&&FVector::DotProduct(GetActorForwardVector(),(Hit.Source->GetActorLocation()-GetActorLocation()).GetSafeNormal())>.25;
    if(const auto* Source=::Cast<AAetherCharacter>(Hit.Source); Source && Source->Fighter==EAetherFighter::Player && Fighter==EAetherFighter::Player)return;
    Super::ReceiveEquipmentHit_Implementation(Hit);
    if(auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>())
    { M->CreditHit(this,Cast<AAetherCharacter>(Hit.Source)); if(Blocked)M->Observe(this,"Block"); }
}
void AAetherFrontierCharacter::Tick(float Dt)
{
    Super::Tick(Dt);
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
    if(auto* PS=ProfileState())MaxHealth=100+5*FMath::Clamp(PS->Profile.Experience/200,0,4);
    if(ReviveTarget)
    {
        if(!IsValid(ReviveTarget)||!Alive()||ReviveTarget->Alive()||DamageReceivedCount!=ReviveDamageSerial||CombatTime()<StunUntil||FVector::DistSquared(GetActorLocation(),ReviveTarget->GetActorLocation())>FMath::Square(220.0))ReviveTarget=nullptr;
        if(ReviveTarget){FCollisionQueryParams Q(SCENE_QUERY_STAT(RescueLOS),false,this);Q.AddIgnoredActor(ReviveTarget);if(GetWorld()->LineTraceTestByChannel(GetActorLocation(),ReviveTarget->GetActorLocation(),ECC_Visibility,Q))ReviveTarget=nullptr;}
        if(!ReviveTarget)if(auto* Spec=AbilitySystem->FindAbilitySpecFromClass(UAetherReviveAbility::StaticClass()))AbilitySystem->CancelAbilityHandle(Spec->Handle);
    }
    if(!IsValid(CompanionOwner)||!Alive()||CombatTime()<StunUntil||bCompanionHold)return;
    if(auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();M&&M->Encounters&&M->Encounters->IsChanneling(this))return;
    const float T=CombatTime();
    if(!CompanionOwner->Alive())
    {
        const FVector D=CompanionOwner->GetActorLocation()-GetActorLocation();
        if(D.Size2D()>170)AddMovementInput(SafeMoveDirection(CompanionOwner->GetActorLocation()));
        else if(!ReviveTarget){ReviveTarget=CompanionOwner;ReviveStarted=T;ReviveDamageSerial=DamageReceivedCount;if(!AbilitySystem->TryActivateAbilityByClass(UAetherReviveAbility::StaticClass()))ReviveTarget=nullptr;}
        return;
    }
    if(bHealer&&T>NextCompanionAction&&CompanionOwner->Health()<65&&Mana()>=15&&FVector::DistSquared(GetActorLocation(),CompanionOwner->GetActorLocation())<FMath::Square(600.0))
    {NextCompanionAction=T+5;ExecuteCompanionHeal(CompanionOwner.Get());}
    AAetherCharacter* Target=nullptr;double Best=FMath::Square(900.0);
    for(TActorIterator<AAetherCharacter> It(GetWorld());It;++It)if(It->Fighter!=EAetherFighter::Player&&It->Alive())
    {double D=FVector::DistSquared(GetActorLocation(),It->GetActorLocation());if(D<Best)
     {auto* FC=Cast<AAetherFrontierCharacter>(*It);auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();if(FC&&!FC->EncounterId.IsNone()&&M&&M->Encounters&&!M->Encounters->Participates(this,FC->EncounterId))continue;
      FCollisionQueryParams Q(SCENE_QUERY_STAT(CompanionSight),false,this);Q.AddIgnoredActor(*It);if(GetWorld()->LineTraceTestByChannel(GetActorLocation(),It->GetActorLocation(),ECC_Visibility,Q))continue;Best=D;Target=*It;}}
    const FVector D=(Target?Target->GetActorLocation():CompanionOwner->GetActorLocation())-GetActorLocation();
    if(!Target){if(D.Size2D()>400)bFollowing=true;else if(D.Size2D()<250)bFollowing=false;}
    if(Target?D.Size2D()>145:bFollowing)
    {
        FHitResult Wall;FCollisionQueryParams Q(SCENE_QUERY_STAT(CompanionMove),false,this);Q.AddIgnoredActor(CompanionOwner);if(Target)Q.AddIgnoredActor(Target);
        FVector Dir=D.GetSafeNormal2D();
        if(GetWorld()->LineTraceSingleByChannel(Wall,GetActorLocation(),GetActorLocation()+Dir*160,ECC_Visibility,Q))Dir=FVector::CrossProduct(Wall.ImpactNormal,FVector::UpVector).GetSafeNormal();
        AddMovementInput(SafeMoveDirection(Target?Target->GetActorLocation():CompanionOwner->GetActorLocation()));
    }
    if(Target){SetActorRotation(D.Rotation());if(Controller)Controller->SetControlRotation(D.Rotation());if(T>NextCompanionAction&&Ready()){PerformMelee(false);NextCompanionAction=T+1;}}
}

void AAetherFrontierCharacter::ExecuteCompanionHeal(TWeakObjectPtr<AAetherFrontierCharacter> Target)
{
    auto* Recipient=Target.Get();
    if(!HasAuthority()||!Recipient||Recipient!=CompanionOwner||!bHealer||!Alive()||!Recipient->Alive()||
        Mana()<15||FVector::DistSquared(GetActorLocation(),Recipient->GetActorLocation())>FMath::Square(600.))return;
    const TWeakObjectPtr<AAetherFrontierCharacter> Self=this;
    if(Recipient->ResourceGate->Defer([Self,Target]{if(Self.IsValid())Self->ExecuteCompanionHeal(Target);}))return;
    if(ResourceGate->Defer([Self,Target]{if(Self.IsValid())Self->ExecuteCompanionHeal(Target);}))return;
    // 整个治疗及施法者扣费一起延后，以执行时的当前生命加增量，不排队旧绝对目标。
    Recipient->SetVitals(Recipient->Health()+20,Recipient->Mana(),Recipient->Stamina());
    AbilitySystem->ApplyModToAttribute(UAetherAttributes::GetManaAttribute(),EGameplayModOp::Additive,-15);
}
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
 if(UsesNativeSkills()){FString Why;AetherNativeInventory::Shortcut(*this,"Claim",NAME_None,Why);Feedback=Why;OnPresentationChanged.Broadcast();}
 else ServerAction("Claim");
}
void AAetherFrontierCharacter::CycleItem()
{
 if(!bPanel||!ProfileState())return;
 if(Panel==1&&UsesNativeSkills()){FString Why;AetherNativeInventory::Shortcut(*this,"Next",NAME_None,Why);OnPresentationChanged.Broadcast();return;}
 if(Panel==1&&!ProfileState()->Profile.Inventory.IsEmpty()){SelectedItem=(SelectedItem+1)%ProfileState()->Profile.Inventory.Num();SelectedInstance=ProfileState()->Profile.Inventory[SelectedItem].InstanceId;}
 if(Panel==2)TrackedQuest=AetherGuide::SelectQuest(ProfileState()->Profile,TrackedQuest,true);
 OnPresentationChanged.Broadcast();
}

bool AAetherFrontierCharacter::AllowsGeneratedMotion() const
{
    // 场景交互占用由玩法所有者明确提供，动作插件不反查任务或持久化。
    return Super::AllowsGeneratedMotion()&&!Carried&&!ReviveTarget&&!bTravelPending;
}
