#pragma once
#include "Skills/AetherSkillState.h"

struct FAetherResolvedSkillGrant
{
    FString SkillId;
    int32 PermanentRank=0,EffectiveRank=0;
    TArray<FString> EffectiveSourceIds,SuppressedSourceIds;
};
namespace AetherSkillGrants
{
    inline TArray<FAetherResolvedSkillGrant> Resolve(const FAetherSkillStateV10& State,
        const FAetherSkillDefinitionsV10& Definitions,const TArray<FAetherExternalSkillGrant>& Sources)
    {
        TArray<FString> Ids;Definitions.Skills.GetKeys(Ids);Ids.Sort();TArray<FAetherResolvedSkillGrant> Out;
        for(const auto& Id:Ids)
        {
            FAetherResolvedSkillGrant R;R.SkillId=Id;R.PermanentRank=State.PermanentRank(Id);R.EffectiveRank=State.EffectiveRank(Id,Sources);
            if(R.PermanentRank>0)(R.PermanentRank==R.EffectiveRank?R.EffectiveSourceIds:R.SuppressedSourceIds).Add(TEXT("Permanent.")+Id);
            for(const auto& S:Sources)if(S.SkillId==Id)(S.Rank==R.EffectiveRank?R.EffectiveSourceIds:R.SuppressedSourceIds).Add(S.SourceId);
            R.EffectiveSourceIds.Sort();R.SuppressedSourceIds.Sort();Out.Add(MoveTemp(R));
        }
        return Out;
    }
    inline FString EffectiveSignature(const TArray<FAetherResolvedSkillGrant>& Grants)
    {
        FString Key;for(const auto& G:Grants)Key+=G.SkillId+TEXT(":")+FString::FromInt(G.EffectiveRank)+TEXT(":")+FString::Join(G.EffectiveSourceIds,TEXT(","))+TEXT(";");return Key;
    }
}
