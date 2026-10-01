#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AetherStartupSettings.generated.h"
class UWorld;

// 必需参数来自显式项目配置；无有效前端资产时不提供隐式返回地图。
UCLASS(Config=Game,DefaultConfig)
class AETHERGAMEPLAY_API UAetherStartupSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config,EditAnywhere,Category="Startup") double BackendDrainTimeoutSeconds=0;
    UPROPERTY(Config,EditAnywhere,Category="Startup") TSoftObjectPtr<UWorld> FrontendMap;
    UPROPERTY(Config,EditAnywhere,Category="Startup") TSoftObjectPtr<UWorld> PlayableMap;
    bool DrainDeadline(double Now,double& Deadline,FString& Reason) const;
    bool ResolveMap(bool Frontend,FString& Package) const;
};
