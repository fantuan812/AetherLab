#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AetherVaultAbility.generated.h"
class AAetherFrontierCharacter;
namespace AetherVault { AETHERLAB_API FGameplayTag ActiveTag(); }
// 跳跃入口在有合法低障碍时启动；两端分别查询场景，客户端不上传目标位置。
UCLASS()
class AETHERLAB_API UAetherVaultAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UAetherVaultAbility();
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,const FGameplayTagContainer* Source=nullptr,const FGameplayTagContainer* Target=nullptr,FGameplayTagContainer* Relevant=nullptr) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,const FGameplayEventData* Event) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,bool Replicate,bool Cancelled) override;
    virtual void OnAvatarSet(const FGameplayAbilityActorInfo* Info,const FGameplayAbilitySpec& Spec) override;
private:
    static bool FindPath(AAetherFrontierCharacter& C,TArray<FVector>& Points,FVector* Contact=nullptr);
    UFUNCTION() void NextPhase();
    UFUNCTION() void Abort();
    void CheckInterruption();
    TWeakObjectPtr<AAetherFrontierCharacter> Character;
    TArray<FVector> Path;
    FTimerHandle Watch;
    int32 Phase=0;
    float DamageAtStart=0;
};
