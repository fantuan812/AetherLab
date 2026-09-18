#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AetherPlayerController.generated.h"

// 专服只发送原生 AHUD，真正的表现类由拥有该连接的客户端选择。
// 这样服务器无需为 HUD RPC 加载 UMG 或引用客户端脚本包。
UCLASS()
class AETHERLAB_API AAetherPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void ClientSetHUD_Implementation(TSubclassOf<AHUD> NewHUDClass) override;
    virtual void SpawnDefaultHUD() override;
};
