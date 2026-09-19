#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "AetherDodgeAbility.generated.h"

class AAetherCharacter;
namespace AetherDodge
{
    AETHERLAB_API FGameplayTag ActiveTag();
    AETHERLAB_API FGameplayTag InvulnerableTag();
}
UCLASS()
class UAetherDodgeCost : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UAetherDodgeCost();
};
UCLASS()
class UAetherDodgeCooldown : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UAetherDodgeCooldown();
};
UCLASS()
class UAetherDodgeInvulnerability : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UAetherDodgeInvulnerability();
};

// 预测成本由 GAS 的 GameplayEffect 负责；移动由引擎可复制的 RootMotionSource 负责。
UCLASS()
class AETHERLAB_API UAetherDodgeAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UAetherDodgeAbility();
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,
        const FGameplayTagContainer* Source=nullptr,const FGameplayTagContainer* Target=nullptr,FGameplayTagContainer* Relevant=nullptr) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation,const FGameplayEventData* Event) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation,bool Replicate,bool Cancelled) override;
    virtual void OnAvatarSet(const FGameplayAbilityActorInfo* Info,const FGameplayAbilitySpec& Spec) override;
private:
    UFUNCTION() void FinishRecovery();
    TWeakObjectPtr<AAetherCharacter> ActiveCharacter;
    TWeakObjectPtr<UAbilitySystemComponent> ActiveSystem;
    FActiveGameplayEffectHandle Invulnerability;
};
