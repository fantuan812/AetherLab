#include "AetherProgression.h"
bool FAetherSkillGrantSnapshot::NetSerialize(FArchive& Ar,UPackageMap*,bool& Success)
{
    Success=false;uint16 Count=uint16(Rows.Num());
    if(Ar.IsSaving()&&Rows.Num()>256){Ar.SetError();return false;}
    Ar<<ProfileRevision<<Sequence<<Count;
    if(Count>256||ProfileRevision < -1||ProfileRevision==MAX_int64){Ar.SetError();return false;}
    if(Ar.IsLoading())Rows.SetNum(Count);
    for(auto& Row:Rows)
    {
        Ar<<Row.SourceId<<Row.SkillId<<Row.Rank<<Row.Source<<Row.InstanceId<<Row.ExpiresAtServerSeconds;
        if(Row.SourceId.IsEmpty()||Row.SourceId.Len()>128||Row.SkillId.IsEmpty()||Row.SkillId.Len()>96||
           Row.Rank<1||Row.Rank>100||Row.Source>uint8(EAetherSkillGrantSource::Temporary)||
           !FMath::IsFinite(Row.ExpiresAtServerSeconds)||Row.ExpiresAtServerSeconds<0)
        {Ar.SetError();return false;}
    }
    Success=!Ar.IsError();return Success;
}
