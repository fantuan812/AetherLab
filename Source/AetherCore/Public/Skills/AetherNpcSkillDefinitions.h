#pragma once
#include "Skills/AetherSkillDefinitions.h"

struct FAetherNpcSkillGrant
{
    FString SkillId;
    int32 Rank=1,Slot=-1;
};
struct FAetherNpcSkillLoadout
{
    FString Id;
    TArray<FAetherNpcSkillGrant> InitialGrants;
};
// Actor-definition capability data has no profile, inventory, reward or persistence authority.
struct AETHERCORE_API FAetherNpcSkillDefinitions
{
    TMap<FString,FAetherNpcSkillLoadout> Loadouts;
    TMap<FString,FString> FighterLoadouts;
    bool bValid=false;
    FString Error;
    const FAetherNpcSkillLoadout* Find(const FString& Id) const;
    const FAetherNpcSkillLoadout* ForFighter(const FString& Fighter) const;
    static FAetherNpcSkillDefinitions Parse(const FString& Json,const FAetherSkillDefinitionsV10& Skills);
    static const FAetherNpcSkillDefinitions& Get();
};
