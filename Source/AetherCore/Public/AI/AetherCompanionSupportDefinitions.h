#pragma once
#include "CoreMinimal.h"
#include "Skills/AetherSkillDefinitions.h"
#include "Effects/AetherBuffState.h"

enum class EAetherCompanionSupportPurpose:uint8 { SelfHealing, FriendlyHealing, Cooling };
enum class EAetherCompanionCoolingPriority:uint8 { BeforeHealing, AfterHealing };
struct FAetherCompanionSupportSkill
{
    bool bEnabled=false;
    FString SkillId;
};
struct AETHERCORE_API FAetherCompanionSupportProfile
{
    FString Id;
    double SampleIntervalSeconds=0,PatientHealthRatioBelow=0,PatientSearchRadiusCm=0,CoolingAboveTemperatureC=0;
    FAetherCompanionSupportSkill SelfHealing,FriendlyHealing,Cooling;
    EAetherCompanionCoolingPriority CoolingPriority=EAetherCompanionCoolingPriority::BeforeHealing;
    const FAetherCompanionSupportSkill& Skill(EAetherCompanionSupportPurpose Purpose) const;
    bool ValidateSkills(const FAetherSkillDefinitionsV10& Skills,const FAetherBuffDefinitions& Buffs,FString& Reason) const;
};
// 仅保存同行者支持决策；成本、范围、前摇、恢复与CD始终由正式Spec/技能数据决定。
struct AETHERCORE_API FAetherCompanionSupportDefinitions
{
    TMap<FString,FAetherCompanionSupportProfile> Profiles;
    TMap<FString,FString> LoadoutProfiles;
    bool bValid=false;
    FString Error;
    const FAetherCompanionSupportProfile* ForLoadout(const FString& Loadout) const;
    static FAetherCompanionSupportDefinitions Parse(const FString& Json);
    static const FAetherCompanionSupportDefinitions& Get();
};
