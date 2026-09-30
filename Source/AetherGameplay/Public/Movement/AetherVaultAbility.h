#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AetherVaultAbility.generated.h"
class AAetherFrontierCharacter;
class UAbilityTask_ApplyRootMotionMoveToForce;
namespace AetherVault { AETHERGAMEPLAY_API FGameplayTag ActiveTag(); }
// 跳跃入口在有合法低障碍时启动；两端分别查询场景，客户端不上传目标位置。
UCLASS()
class AETHERGAMEPLAY_API UAetherVaultAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UAetherVaultAbility();
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,const FGameplayTagContainer* Source=nullptr,const FGameplayTagContainer* Target=nullptr,FGameplayTagContainer* Relevant=nullptr) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,const FGameplayEventData* Event) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,bool Replicate,bool Cancelled) override;
    virtual void OnAvatarSet(const FGameplayAbilityActorInfo* Info,const FGameplayAbilitySpec& Spec) override;
private:
    friend class FAetherVaultLifecycleTest;
    static bool FindPath(AAetherFrontierCharacter& C,TArray<FVector>& Points,FVector* Contact=nullptr);
    UFUNCTION() void NextPhase();
    UFUNCTION() void Abort();
    UFUNCTION() void MovementModeChanged(ACharacter* ChangedCharacter,EMovementMode PreviousMode,uint8 PreviousCustomMode);
    void CheckInterruption();
    bool HasActivationAvatar() const;
    void ReleaseMovement();
    TWeakObjectPtr<AAetherFrontierCharacter> Character;
    TWeakObjectPtr<UCharacterMovementComponent> Movement;
    TWeakObjectPtr<UAbilitySystemComponent> ActiveSystem;
    TWeakObjectPtr<UAbilityTask_ApplyRootMotionMoveToForce> MotionTask;
    TArray<FVector> Path;
    FTimerHandle Watch;
    int32 Phase=0;
    float DamageAtStart=0;
    uint32 OwnedActionSerial=0;
    bool bOwnsFlyingMode=false;
    bool bEndingVault=false;
};
