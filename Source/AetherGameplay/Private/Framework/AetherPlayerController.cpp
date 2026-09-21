#include "Framework/AetherPlayerController.h"
#include "Tests/AetherNativeNetworkProbe.h"
#include "Tests/AetherNativeSoakProbe.h"
#include "Tests/AetherMotionQualityProbe.h"
#include "Tests/AetherNativeJourneyProbe.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Presentation/AetherPresentation.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Networking/AetherCommandRuntime.h"
#include "Networking/AetherCommandClient.h"
#include "GameFramework/HUD.h"
#include "Characters/AetherFrontierCharacter.h"

void AAetherPlayerController::ClientSetHUD_Implementation(TSubclassOf<AHUD> NewHUDClass)
{
    if (IsLocalController() && GetNetMode() != NM_DedicatedServer)
    {
        // 只替换默认 HUD；地图显式配置的特殊 HUD 仍由地图负责。
        if (!NewHUDClass || NewHUDClass == AHUD::StaticClass())
            if (UClass* Presentation = AetherPresentation::ResolveHUD()) NewHUDClass = Presentation;
    }
    Super::ClientSetHUD_Implementation(NewHUDClass);
}
void AAetherPlayerController::SpawnDefaultHUD()
{
    if (IsLocalController() && !GetHUD())
        ClientSetHUD_Implementation(AHUD::StaticClass());
    Super::SpawnDefaultHUD();
}

void AAetherPlayerController::FlushPressedKeys()
{
    // 先撤销攻击保持，再让 Enhanced Input 派发 Canceled，避免失焦被当作松键攻击。
    if(auto* C=Cast<AAetherFrontierCharacter>(GetPawn()))C->ReleaseHeldInput();
    Super::FlushPressedKeys();
}

void AAetherPlayerController::BindMenuPawn()
{
    if(auto* LP=GetLocalPlayer())LP->GetSubsystem<UAetherMenuSubsystem>()->AttachPawn(Cast<AAetherFrontierCharacter>(GetPawn()));
}
void AAetherPlayerController::BeginPlay()
{
    Super::BeginPlay();BindMenuPawn();
    if(IsLocalController())IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddWeakLambda(this,
        [this](EInputDeviceConnectionState State,FPlatformUserId User,FInputDeviceId)
        {
            if(State==EInputDeviceConnectionState::Disconnected&&GetLocalPlayer()&&GetLocalPlayer()->GetPlatformUserId()==User)
                FlushPressedKeys();
        });
}
void AAetherPlayerController::SetPawn(APawn* InPawn)
{
    Super::SetPawn(InPawn);BindMenuPawn();
    if(HasAuthority())if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->NotifyPawnChanged(this);
}
void AAetherPlayerController::OnRep_Pawn(){Super::OnRep_Pawn();BindMenuPawn();}
void AAetherPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().RemoveAll(this);
    FlushPressedKeys();
    if(auto* LP=GetLocalPlayer())
        if(auto* Menu=LP->GetSubsystem<UAetherMenuSubsystem>();Menu->GetBoundPawn()==GetPawn())Menu->AttachPawn(nullptr);
    if(HasAuthority())if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->UnbindPlayer(this);
    if(auto* LP=GetLocalPlayer())LP->GetSubsystem<UAetherCommandClient>()->DetachController(this);
    Super::EndPlay(Reason);
}

void AAetherPlayerController::PlayerTick(float DeltaSeconds)
{Super::PlayerTick(DeltaSeconds);AetherNativeNetworkProbe::Tick(this);AetherNativeSoakProbe::Tick(this,DeltaSeconds);AetherMotionQualityProbe::Tick(this,DeltaSeconds);AetherNativeJourneyProbe::Tick(this);}
