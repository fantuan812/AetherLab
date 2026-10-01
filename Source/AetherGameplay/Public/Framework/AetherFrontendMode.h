#pragma once
#include "GameFramework/GameModeBase.h"
#include "AetherFrontendMode.generated.h"

// 正式开始页的无持久化停留点。只提供本地UI上下文，不是网络大厅或账号服务。
UCLASS()
class AETHERGAMEPLAY_API AAetherFrontendMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AAetherFrontendMode();
};
