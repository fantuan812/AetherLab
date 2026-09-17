#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AetherActions.generated.h"
class AAetherFrontierCharacter;
UCLASS()
class UAetherMeleeAbility:public UGameplayAbility
{
 GENERATED_BODY()
public:
 UAetherMeleeAbility();
 virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,const FGameplayEventData* Event) override;
};
UCLASS()
class UAetherReviveAbility:public UGameplayAbility
{
 GENERATED_BODY()
public:
 UAetherReviveAbility();
 virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,const FGameplayEventData* Event) override;
 virtual void EndAbility(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,bool Replicate,bool Cancelled) override;
private:
 UPROPERTY() TObjectPtr<AAetherFrontierCharacter> Reviver;
 UPROPERTY() TObjectPtr<AAetherFrontierCharacter> RescueTarget;
 UFUNCTION() void FinishRevive();
};
