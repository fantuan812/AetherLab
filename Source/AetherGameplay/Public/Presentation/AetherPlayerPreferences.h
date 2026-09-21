#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AetherPlayerPreferences.generated.h"
/** 玩家本地偏好，不与服务器任务、金币或角色档案共用写权限。 */
UCLASS(Config=GameUserSettings)
class AETHERGAMEPLAY_API UAetherPlayerPreferences : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config) float MasterVolume=.8f;
    UPROPERTY(Config) float MouseSensitivity=1;
    UPROPERTY(Config) float ControllerSensitivity=1;
    UPROPERTY(Config) float UIScale=1;
    UPROPERTY(Config) bool bInvertLook=false;
    UPROPERTY(Config) int32 MotionBackend=-1;
};
