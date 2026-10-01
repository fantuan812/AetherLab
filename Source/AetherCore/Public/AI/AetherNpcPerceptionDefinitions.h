#pragma once
#include "CoreMinimal.h"

struct AETHERCORE_API FAetherNpcPerceptionProfile
{
    FString Id;
    double SampleIntervalSeconds=0,SightRadiusCm=0,TargetHomeRadiusCm=0,SelfLeashRadiusCm=0;
    double MemorySeconds=0,ObservationFreshnessSeconds=0,HomeArrivalRadiusCm=0;
    // 本采样保持算法要求 Freshness>=Interval，避免两次正常采样之间反复进入 Search。
    // 这是当前算法合同，不是所有 AI 感知系统的通用限制。
    bool IsValid() const;
};
// 只拥有敌方感知参数；不授予能力、不持有冷却/伤害，也不覆盖同行者策略。
struct AETHERCORE_API FAetherNpcPerceptionDefinitions
{
    TMap<FString,FAetherNpcPerceptionProfile> Profiles;
    TMap<FString,FString> FighterProfiles;
    bool bValid=false;
    FString Error;
    const FAetherNpcPerceptionProfile* Find(const FString& Id) const;
    const FAetherNpcPerceptionProfile* ForFighter(const FString& Fighter) const;
    static FAetherNpcPerceptionDefinitions Parse(const FString& Json);
    static const FAetherNpcPerceptionDefinitions& Get();
};
