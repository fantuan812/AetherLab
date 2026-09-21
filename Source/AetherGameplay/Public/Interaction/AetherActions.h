#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AetherEquipmentComponent.h"
#include "AetherActions.generated.h"
class AAetherFrontierCharacter;
UCLASS()
class UAetherMeleeAbility:public UGameplayAbility
{
 GENERATED_BODY()
public:
 UAetherMeleeAbility();
 virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,const FGameplayEventData* Event) override;
 virtual bool CheckCost(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,FGameplayTagContainer* Tags=nullptr) const override;
 virtual void ApplyCost(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation) const override;
 virtual void EndAbility(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,bool Replicate,bool Cancelled) override;
 virtual void OnAvatarSet(const FGameplayAbilityActorInfo* Info,const FGameplayAbilitySpec& Spec) override;
private:
 void AttackFinished(uint32 Serial,bool Cancelled);
 void AttackPhaseChanged(uint32 Serial,EAetherAttackPhase Phase);
 void ClearPhaseTag();
 TWeakObjectPtr<UAetherEquipmentComponent> ActiveEquipment;
 TWeakObjectPtr<UAbilitySystemComponent> ActiveSystem;
 FDelegateHandle FinishedDelegate,PhaseDelegate;
 FGameplayTag PhaseTag;
 uint32 ActiveSerial=0;
 float PreparedCost=0;
 mutable bool bCostApplied=false;
 bool bEnding=false;

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
