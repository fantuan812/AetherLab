#pragma once
#include "CoreMinimal.h"
#include "AI/AetherNpcMeleeDefinitions.h"
class AAetherCharacter;
class AController;
class UAbilitySystemComponent;
class UAetherEquipmentComponent;

// 请求身份只撤销AI意图；真正攻击身份由Equipment.ExecutionId/Serial生成并归GAS持有。
struct FAetherNpcMeleeIntent
{
    FGuid RequestId;
    TWeakObjectPtr<AAetherCharacter> Target;
    TWeakObjectPtr<AController> Controller;
    TWeakObjectPtr<UAbilitySystemComponent> System;
    TWeakObjectPtr<UAetherEquipmentComponent> Equipment;
    FName AttackId,ItemId;
    int32 LoadoutRevision=0;
    double RangeMarginCm=0,MotionSpeedCmPerSecond=0;
    EAetherNpcAttackMotion Motion=EAetherNpcAttackMotion::None;
};
enum class EAetherNpcMeleeChoice:uint8 {Unavailable,Approach,Telegraph,Ready};
class AETHERGAMEPLAY_API FAetherNpcMeleeDecision
{
public:
    EAetherNpcMeleeChoice Choose(AAetherCharacter& C,AAetherCharacter& Target,const FVector& ObservedPosition,const FAetherNpcMeleeProfile& Profile);
    bool TryExecute(AAetherCharacter& C);
    bool IsIssuing() const{return bIssuing;}
    bool CaptureIssued(const AAetherCharacter& C,FName AttackId,FAetherNpcMeleeIntent& Out) const;
    bool ValidateIssued(const AAetherCharacter& C,const FAetherNpcMeleeIntent& Intent) const;
    void Reset();
private:
    FAetherNpcMeleeIntent Pending;
    FString ProfileId;
    double TelegraphUntil=0;
    bool bIssuing=false;
};
