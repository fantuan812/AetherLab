#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AetherDodgeAbility.generated.h"

class AAetherCharacter;
// 只传递有界单位方向；速度、时长、成本和无敌窗始终由能力定义决定。
USTRUCT()
struct FAetherDodgeDirection : public FGameplayAbilityTargetData
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantizeNormal Direction;
    virtual UScriptStruct* GetScriptStruct() const override{return StaticStruct();}
    bool NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success){return Direction.NetSerialize(Ar,Map,Success);}
};
template<> struct TStructOpsTypeTraits<FAetherDodgeDirection> : TStructOpsTypeTraitsBase2<FAetherDodgeDirection>
{enum {WithNetSerializer=true,WithCopy=true};};
namespace AetherDodge
{
    AETHERGAMEPLAY_API FGameplayTag ActiveTag();
    AETHERGAMEPLAY_API FGameplayTag InvulnerableTag();
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
class AETHERGAMEPLAY_API UAetherDodgeAbility : public UGameplayAbility
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
    UFUNCTION() void DirectionTimeout();
    void ReceiveDirection(const FGameplayAbilityTargetDataHandle& Data,FGameplayTag Tag);
    bool CanCommitDodge(const AAetherCharacter* Character,const FGameplayAbilityActorInfo* Info,bool bOwnDodgeActive) const;
    void StartMotion(const FVector& Direction);
    FDelegateHandle DirectionDelegate;
    bool bMotionStarted=false;
    bool bAwaitingDirection=false;
    uint32 ActivationActionSerial=0;
    TWeakObjectPtr<AAetherCharacter> ActiveCharacter;
    TWeakObjectPtr<UAbilitySystemComponent> ActiveSystem;
    FActiveGameplayEffectHandle Invulnerability;
};
