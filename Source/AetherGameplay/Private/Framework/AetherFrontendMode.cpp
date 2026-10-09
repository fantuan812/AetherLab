#include "Framework/AetherFrontendMode.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"

AAetherFrontendMode::AAetherFrontendMode()
{
    DefaultPawnClass=nullptr;HUDClass=AHUD::StaticClass();PlayerControllerClass=APlayerController::StaticClass();
    bStartPlayersAsSpectators=true;
    // 不调用 Prepare、安装后端或主动关闭旧 Runtime；GameInstance仍可排空此前已接受的事务。
}
