#pragma once
#include "CoreMinimal.h"
#include "AetherAdventure.h"
#include "AetherFrontierState.generated.h"

class AAetherFrontierProp;
class AAetherFrontierCharacter;
class UAetherPhysicsDamageComponent;
class UAetherTraversalComponent;
class UPhysicsHandleComponent;
class UInputMappingContext;
class UInputAction;
class UAetherFrontierPanel;
struct FAetherWorldPlacement;

// 只复制公开世界事实；个人背包和任务不放入全局 GameState。
UCLASS()
class AETHERLAB_API AAetherFrontierState : public AAetherAdventureState
{
    GENERATED_BODY()
public:
    UPROPERTY(Replicated) bool bSupplyRestored = false;
    UPROPERTY(Replicated) bool bWorkshopRestored = false;
    UPROPERTY(Replicated) bool bBridgeReleased = false;
    UPROPERTY(Replicated) bool bPowerOn = true;
    UPROPERTY(Replicated) int32 ActivityKills = 0;
    UPROPERTY(Replicated) int32 ClosurePhase = 0;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
