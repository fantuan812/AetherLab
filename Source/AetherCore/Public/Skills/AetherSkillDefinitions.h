#pragma once
#include "CoreMinimal.h"

enum class EAetherSkillMechanic : uint8 { Fire, Water, Frost, Lightning };
struct FAetherSkillRankEffect
{
    int32 PointCost=0, RequiredLevel=1;
    double ManaCost=0, Cooldown=0, RangeCm=0, TargetRadiusCm=12;
    double HeatJ=0, WaterKg=0, ElectricalJ=0;
    TMap<FString,double> PassiveStats; // 有限白名单被动，绝不混作施法输出。
};
struct FAetherSkillPrerequisite { FString SkillId; int32 Rank=1; };
struct FAetherSkillDefinitionV10
{
    FString SkillId, DisplayName, IconId, RequiredQuest;
    EAetherSkillMechanic Mechanic=EAetherSkillMechanic::Fire;
    bool bActive=true, bStoryBase=true;
    TArray<FAetherSkillRankEffect> Ranks;
    TArray<FAetherSkillPrerequisite> Prerequisites;
    // 仅供冻结 v9 bitmask/旧快捷键兼容映射，不能作为 GAS AbilityLevel。
    int32 LegacyBit=-1;
};
struct AETHERCORE_API FAetherSkillDefinitionsV10
{
    int32 ContentSchemaVersion=3;
    TMap<FString,FAetherSkillDefinitionV10> Skills;
    bool Validate(FString& Reason) const;
    const FAetherSkillDefinitionV10* Legacy(int32 Bit) const;
    const FAetherSkillRankEffect* Effect(const FString& SkillId,int32 Rank) const;
    static FAetherSkillDefinitionsV10 Parse(const FString& Json,FString& Reason);
    static const FAetherSkillDefinitionsV10& Get();
};
