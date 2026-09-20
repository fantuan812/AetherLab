#pragma once
#include "CoreMinimal.h"
#include "AetherAdventure.h"
#include "ReactiveMechanismComponent.h"
#include "AetherWorldCapability.h"
#include "AetherFrontierProp.generated.h"

class AAetherFrontierProp;
class AAetherFrontierCharacter;
class UAetherPhysicsDamageComponent;
class UAetherTraversalComponent;
class UPhysicsHandleComponent;
class UInputMappingContext;
class UInputAction;
class UAetherFrontierPanel;
struct FAetherWorldPlacement;

// 世界交互实体拥有组件与复制状态，持久身份由 Spec.Id 表达。
UCLASS()
class AETHERLAB_API AAetherFrontierProp : public AAetherWorldObject, public IAetherWorldCapability
{
    GENERATED_BODY()
public:
    AAetherFrontierProp();
    virtual bool HasWorldCapability(FName Capability) const override {return Capability=="LiquidReceiver"?bAcceptsWater:Capabilities.Contains(Capability);}
    virtual UReactiveBodyComponent* ReactionBody() const override {return Reactive;}
    UPROPERTY(VisibleAnywhere) TObjectPtr<UReactiveMechanismComponent> Mechanism;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAetherPhysicsDamageComponent> PhysicsDamage;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAetherTraversalComponent> Traversal;
    // 每个场景实例独立版本，卸载后同一稳定 ID 重建也会使旧选择失效。
    UPROPERTY(Replicated) int64 InteractionRevision=0;
    UPROPERTY(Replicated) FName Service;
    UPROPERTY(Replicated) TArray<FName> Capabilities;
    UPROPERTY(Replicated) bool bCarryable = false;
    UPROPERTY(Replicated) TObjectPtr<AAetherCharacter> Carrier;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Person;
    UPROPERTY(Replicated) float ReceivedPower = 0;
    UPROPERTY(Replicated) bool bAcceptsWater = false;
    UPROPERTY(Replicated) bool bInspectableFire = false;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit) override;
    UFUNCTION() void OnMaterialReaction(EReactiveReaction Kind,double Magnitude,FVector Vector);
    UFUNCTION() void OnElectricalWindow(const FReactiveElectricalWindow& Window);
    UPROPERTY(Replicated) bool bGlobalPowerService = false;
    UPROPERTY(Replicated) bool bWorkshopService = false;
    UPROPERTY(Replicated) bool bExtinguished = false;
    uint8 LastFeedback = 255;
    void UpdateReactionFeedback();
    bool bWasBurning = false;
    float LastPowerTime = -100;
};
