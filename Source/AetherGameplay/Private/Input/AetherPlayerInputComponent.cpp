#include "Input/AetherPlayerInputComponent.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Input/AetherInputProfile.h"
#include "Presentation/AetherPlayerPreferences.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
UAetherPlayerInputComponent::UAetherPlayerInputComponent()
{PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.1f;}
void UAetherPlayerInputComponent::Detach()
{
    if(auto* Input=BoundInput.Get())for(uint32 Handle:BindingHandles)Input->RemoveBindingByHandle(Handle);
    if(auto* Sub=BoundSubsystem.Get();Sub&&GameplayContext)Sub->RemoveMappingContext(GameplayContext);
    BindingHandles.Reset();BoundInput.Reset();BoundSubsystem.Reset();GameplayContext=nullptr;InputActions.Reset();DefaultBindings.Reset();
}
void UAetherPlayerInputComponent::EndPlay(const EEndPlayReason::Type Reason){Detach();Super::EndPlay(Reason);}
void UAetherPlayerInputComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta,Type,Tick);
    if(auto* C=Cast<AAetherFrontierCharacter>(GetOwner());BoundSubsystem.IsValid()&&(!C||!C->IsLocallyControlled()))Detach();
}
void UAetherPlayerInputComponent::Setup(UInputComponent* I)
{
    Detach();auto* C=Cast<AAetherFrontierCharacter>(GetOwner());if(!C)return;
    auto* Enhanced=Cast<UEnhancedInputComponent>(I);auto* PC=Cast<APlayerController>(C->GetController());
    auto* Sub=PC&&PC->GetLocalPlayer()?ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()):nullptr;
    if(!Enhanced||!Sub)return;
    BoundInput=Enhanced;BoundSubsystem=Sub;
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
    {auto* A=Action(Name,Key,EInputActionValueType::Boolean);BindingHandles.Add(Enhanced->BindAction(A,Event,C,Fn).GetHandle());if(Event==ETriggerEvent::Completed)BindingHandles.Add(Enhanced->BindAction(A,ETriggerEvent::Canceled,C,Name==TEXT("Attack")?&AAetherFrontierCharacter::CancelAttackInput:Fn).GetHandle());};
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
        BindingHandles.Add(Enhanced->BindActionValueLambda(A,ETriggerEvent::Triggered,[C,Name](const FInputActionValue&){if(Name=="Forward")C->Forward(1);else if(Name=="Backward")C->Forward(-1);else C->Right(Name=="Left"?-1:1);}).GetHandle());
    }
    BindingHandles.Add(Enhanced->BindActionValueLambda(Action("LookX",EKeys::MouseX,EInputActionValueType::Axis1D),ETriggerEvent::Triggered,[C](const FInputActionValue& V){C->Yaw(V.Get<float>());}).GetHandle());
    BindingHandles.Add(Enhanced->BindActionValueLambda(Action("LookY",EKeys::MouseY,EInputActionValueType::Axis1D),ETriggerEvent::Triggered,[C](const FInputActionValue& V){C->Pitch(V.Get<float>());}).GetHandle());
    // 手柄视角按秒计算，不混用鼠标每帧位移。局部死区只应用于摇杆轴。
    auto PadAxis=[&](FName Name,FKey Key,TFunction<void(float)> Apply)
    {
        auto* A=Action(Name,Key,EInputActionValueType::Axis1D);
        GameplayContext->UnmapKey(A,Key);
        auto& CleanMapping=GameplayContext->MapKey(A,Key);
        auto* Dead=NewObject<UInputModifierDeadZone>(GameplayContext);Dead->LowerThreshold=.15f;Dead->UpperThreshold=1;
        CleanMapping.Modifiers.Add(Dead);
        BindingHandles.Add(Enhanced->BindActionValueLambda(A,ETriggerEvent::Triggered,[Apply=MoveTemp(Apply)](const FInputActionValue& V){Apply(V.Get<float>());}).GetHandle());
    };
    PadAxis("PadMoveX",EKeys::Gamepad_LeftX,[C](float V){C->Right(V);});
    PadAxis("PadMoveY",EKeys::Gamepad_LeftY,[C](float V){C->Forward(V);});
    PadAxis("PadLookX",EKeys::Gamepad_RightX,[C](float V)
    {if(!C->bPanel)C->AddControllerYawInput(V*120.f*C->GetWorld()->GetDeltaSeconds()*GetDefault<UAetherPlayerPreferences>()->ControllerSensitivity);});
    PadAxis("PadLookY",EKeys::Gamepad_RightY,[C](float V)
    {const auto* P=GetDefault<UAetherPlayerPreferences>();if(!C->bPanel)C->AddControllerPitchInput(V*(P->bInvertLook?1.f:-1.f)*90.f*C->GetWorld()->GetDeltaSeconds()*P->ControllerSensitivity);});
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
    C->OnPresentationChanged.Broadcast();
}
FKey UAetherPlayerInputComponent::BindingFor(FName Name) const
{
 const auto* A=InputActions.Find(Name);if(A&&GameplayContext)for(const auto& M:GameplayContext->GetMappings())if(M.Action==A->Get()&&!M.Key.IsGamepadKey())return M.Key;return FKey();
}
void UAetherPlayerInputComponent::SetBinding(FName Name,FKey Key)
{
 auto* C=Cast<AAetherFrontierCharacter>(GetOwner());
 if(!C||!C->IsLocallyControlled()||!GameplayContext||!Key.IsValid()||Key.IsGamepadKey()||Key.IsAxis1D()||Key.IsAxis2D()||Name=="LookX"||Name=="LookY"||Name=="Escape"||Key==EKeys::Escape)return;
 auto* A=InputActions.Find(Name);if(!A)return;const FKey Old=BindingFor(Name);if(Old==Key)return;
 C->ReleaseHeldInput();
 auto* Profile=GetMutableDefault<UAetherInputProfile>();
 // A single mapping per action: swap conflicts so no command is silently orphaned.
 TArray<FName> Conflicts;for(const auto& Pair:InputActions)if(Pair.Key!=Name&&BindingFor(Pair.Key)==Key&&(UAetherInputProfile::Context(Name)&UAetherInputProfile::Context(Pair.Key)))Conflicts.Add(Pair.Key);
 for(auto Other:Conflicts){auto* OtherAction=InputActions[Other].Get();GameplayContext->UnmapKey(OtherAction,Key);GameplayContext->MapKey(OtherAction,Old);Profile->Keys.Add(Other,Old);}
 GameplayContext->UnmapKey(A->Get(),Old);GameplayContext->MapKey(A->Get(),Key);Profile->Keys.Add(Name,Key);Profile->SaveConfig();
 C->Feedback=FString::Printf(TEXT("Bound %s: %s. Conflicting binding swapped."),*Name.ToString(),*Key.GetDisplayName().ToString());
 if(auto* PC=Cast<APlayerController>(C->GetController()))if(auto* Sub=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))Sub->RequestRebuildControlMappings();
}
