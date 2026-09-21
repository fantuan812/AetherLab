#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AetherCombatComponent.generated.h"
class AAetherCharacter;
class AController;
struct FDamageEvent;
// 身体生命内的命中/格挡/硬直结算和伤害序号；持久属性仍由 PlayerState 的 ASC 拥有。
UCLASS()
class AETHERLAB_API UAetherCombatComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAetherCombatComponent();
    void ReceiveHit(float Damage,float PostureDamage,AAetherCharacter* Source,bool CanBlock);
    void ApplyPostureDamage(float Amount);
    float ApplyDamage(float Amount,const FDamageEvent& Event,AController* Instigator,AActor* Causer);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(Replicated) float LastDamageAt=-100;
    UPROPERTY(Replicated) uint64 DamageReceivedCount=0;
    TWeakObjectPtr<AActor> LastDamager;
    float BlockStarted=-100,NextParryAllowed=0,InvulnerableUntil=0;
};
