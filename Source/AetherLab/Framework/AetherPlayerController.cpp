#include "Framework/AetherPlayerController.h"
#include "Presentation/AetherPresentation.h"
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
