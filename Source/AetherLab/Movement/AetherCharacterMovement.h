#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AetherCharacterMovement.generated.h"

// 玩家移动仍由 CharacterMovement 做碰撞、预测与纠正；输入意图随 SavedMove 传输。
UCLASS()
class AETHERLAB_API UAetherCharacterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    UAetherCharacterMovement();
    UPROPERTY(EditDefaultsOnly,Category="Aether|Movement") float WalkSpeed=450;
    UPROPERTY(EditDefaultsOnly,Category="Aether|Movement") float SprintSpeed=625;
    UPROPERTY(EditDefaultsOnly,Category="Aether|Movement") float CrouchSpeed=190;
    bool bWantsSprint=false;
    bool CanSprint() const;
    bool TryStand();
    virtual float GetMaxSpeed() const override;
    virtual bool CanCrouchInCurrentState() const override;
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
};

class FAetherSavedMove final : public FSavedMove_Character
{
public:
    using Super=FSavedMove_Character;
    bool bSavedWantsSprint=false;
    virtual void Clear() override;
    virtual uint8 GetCompressedFlags() const override;
    virtual bool CanCombineWith(const FSavedMovePtr& NewMove,ACharacter* Character,float MaxDelta) const override;
    virtual void SetMoveFor(ACharacter* C,float Delta,FVector const& Accel,FNetworkPredictionData_Client_Character& Data) override;
    virtual void PrepMoveFor(ACharacter* C) override;
};
