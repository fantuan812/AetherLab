#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AetherGuide.h"
#include "AetherFrontierHUD.generated.h"

class AAetherFrontierProp;
class AAetherFrontierCharacter;
class UAetherPhysicsDamageComponent;
class UAetherTraversalComponent;
class UPhysicsHandleComponent;
class UInputMappingContext;
class UInputAction;
class UAetherFrontierPanel;
struct FAetherWorldPlacement;

// 本地 HUD 入口；与权威角色/存档头文件分离，后续由独立 UI 模块拥有。
UCLASS()
class AAetherFrontierHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    UPROPERTY() TObjectPtr<UAetherFrontierPanel> PanelWidget;
    virtual void DrawHUD() override;
    FAetherGuidance Guidance;
    FAetherInteractionTarget Interaction;
    float NextGuidanceUpdate=0;
};
