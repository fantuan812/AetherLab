#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Skills/AetherCastExecution.h"
#include "AetherSpellAbility.generated.h"
// 同一个 GA 可承载多个技能：Spec 标签决定 SkillId，Level 只表示实际等级。
UCLASS()
class AETHERGAMEPLAY_API UAetherSpellAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UAetherSpellAbility();
    virtual bool CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, FGameplayTagContainer* Tags = nullptr) const override;
    virtual void ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    static float Cost(int32 Spell);
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation,bool Replicate,bool Cancelled) override;
private:
    TOptional<FAetherCastExecution> PreparedCast;
    TSharedPtr<FAetherCastExecution> PaymentExecution;
    TWeakObjectPtr<class AAetherCharacter> PreparedAvatar;
    TWeakObjectPtr<UAbilitySystemComponent> PreparedSystem;
    FTimerHandle WindupTimer;
    mutable bool bPaid=false;
    bool bResultCommitted=false;
    void FinishCast(FGuid ExpectedExecution);
};
