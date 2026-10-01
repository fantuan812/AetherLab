#pragma once
#include "CoreMinimal.h"
enum class EAetherNpcAttackMotion:uint8 {None,ForwardDuringActive};
struct FAetherNpcMeleeHealthBand
{
    // 有序上界：从0开始连续分区，除末行包含1外，上界不包含在本行内。
    double MaxHealthFraction=0,TelegraphSeconds=0;
};
struct AETHERCORE_API FAetherNpcMeleeProfile
{
    FString Id,AttackId;
    bool bGuardWhileApproaching=false;
    double StartRangeMarginCm=0,MotionSpeedCmPerSecond=0;
    EAetherNpcAttackMotion Motion=EAetherNpcAttackMotion::None;
    TArray<FAetherNpcMeleeHealthBand> HealthBands;
    bool IsValid() const;
    bool TelegraphFor(double HealthFraction,double& OutSeconds) const;
};
// 只定义NPC选择/预备/位移内容，不复制装备的成本、距离、命中相位或恢复时间。
struct AETHERCORE_API FAetherNpcMeleeDefinitions
{
    TMap<FString,FAetherNpcMeleeProfile> Profiles;
    TMap<FString,FString> FighterProfiles;
    bool bValid=false;
    FString Error;
    const FAetherNpcMeleeProfile* Find(const FString& Id) const;
    const FAetherNpcMeleeProfile* ForFighter(const FString& Fighter) const;
    static FAetherNpcMeleeDefinitions Parse(const FString& Json);
    static const FAetherNpcMeleeDefinitions& Get();
};
