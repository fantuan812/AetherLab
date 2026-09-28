#include "Effects/AetherBuffState.h"

bool FAetherBuffDefinition::Validate(FString& Why) const
{
    if(Id.IsEmpty()||Id.Len()>96||DisplayName.IsEmpty()||IconId.IsEmpty()||Revision<1||MaxStacks<1||MaxStacks>32||
        !FMath::IsFinite(Duration)||Duration<=0||Duration>3600||!FMath::IsFinite(Period)||Period<0||
        (Period>0&&Period<.1)||Operations.IsEmpty()||Operations.Num()>16||Tags.Num()>16||ImmunityTags.Num()>16||DispelTags.Num()>16||
        uint8(Reapply)>uint8(EAetherBuffReapply::Reject)||uint8(Overflow)>uint8(EAetherBuffOverflow::ReplaceSourceStack))
    {Why=TEXT("Invalid buff definition bounds");return false;}
    for(const auto& O:Operations)
    {
        if(!FMath::IsFinite(O.Value)||FMath::Abs(O.Value)>100000){Why=TEXT("Invalid buff magnitude");return false;}
        if(O.Kind==EAetherBuffOperation::Attribute)
        {
            FAetherResolvedAttributes Out;
            if(!AetherAttributes::Resolve({{O.Id,Id,O.AttributeOperation,O.Value,Priority}},Out,Why))return false;
        }
        else if(O.Kind==EAetherBuffOperation::Skill)
        {
            if(O.Value<1||O.Value>3||O.Value!=FMath::FloorToDouble(O.Value)||!FAetherSkillDefinitionsV10::Get().Effect(O.Id,int32(O.Value)))
            {Why=TEXT("Invalid buff skill grant");return false;}
        }
        else if(O.Kind==EAetherBuffOperation::Tag)
        {
            if(O.Value!=1||(O.Id!=TEXT("Silence")&&O.Id!=TEXT("Stun")&&O.Id!=TEXT("PoisonImmune"))){Why=TEXT("Unknown buff tag");return false;}
        }
        else if((O.Kind==EAetherBuffOperation::Heal||O.Kind==EAetherBuffOperation::Damage)&&O.Id==TEXT("Health")&&Period>0&&O.Value>0){}
        else {Why=TEXT("Unknown or invalid buff operation");return false;}
    }
    Why.Reset();return true;
}
EAetherBuffResult FAetherBuffState::Apply(const FAetherBuffDefinition& D,const FString& Source,double Now,FGuid Delivery,const FString& Instigator)
{
    FString Why;
    if(!LifeId.IsValid()||Source.IsEmpty()||Source.Len()>128||!FMath::IsFinite(Now)||Now<0||!D.Validate(Why))return EAetherBuffResult::Invalid;
    if(Delivery.IsValid()&&Deliveries.Contains(Delivery))return EAetherBuffResult::Replayed;
    if(Delivery.IsValid()&&Deliveries.Num()>=4096)return EAetherBuffResult::Capacity;
    for(const auto& I:Instances)if(!I.bSuppressed)
        for(const auto& Tag:D.Tags)if(I.Definition.ImmunityTags.Contains(Tag))return EAetherBuffResult::Immune;
    auto* Existing=Instances.FindByPredicate([&](const auto& I) {
        return I.Definition.Id==D.Id&&(!D.bPerSource||I.Sources.Contains(Source));
    });
    EAetherBuffResult Result=EAetherBuffResult::Applied;
    if(Existing&&D.Reapply!=EAetherBuffReapply::Independent)
    {
        if(D.Reapply==EAetherBuffReapply::Reject)return EAetherBuffResult::Rejected;
        if(D.Reapply==EAetherBuffReapply::AddStack)
        {
            if(Existing->Stacks()>=D.MaxStacks&&D.Overflow==EAetherBuffOverflow::Reject)return EAetherBuffResult::Capacity;
            if(Existing->Stacks()<D.MaxStacks)++Existing->Sources.FindOrAdd(Source);
            else if(D.Overflow==EAetherBuffOverflow::ReplaceSourceStack)
            {
                // Explicit deterministic overflow policy: replace one stack of the first source ID.
                TArray<FString> Keys;Existing->Sources.GetKeys(Keys);Keys.Sort();
                const FString Key=Keys[0];if(--Existing->Sources.FindChecked(Key)==0)Existing->Sources.Remove(Key);
                ++Existing->Sources.FindOrAdd(Source);
            }
        }
        else if(!Existing->Sources.Contains(Source))
        { // A target-wide refresh transfers no hidden stacks to an unrelated source.
            if(Existing->Stacks()>=D.MaxStacks)return EAetherBuffResult::Capacity;
            Existing->Sources.Add(Source,1);
        }
        Existing->ExpiresAt=D.Reapply==EAetherBuffReapply::Extend?FMath::Min(Now+3600,Existing->ExpiresAt+D.Duration):Now+D.Duration;
        if(D.bResetPeriod&&D.Period>0)Existing->NextTickAt=Now+D.Period;
        ++Existing->Revision;Result=EAetherBuffResult::Refreshed;
    }
    else
    {
        if(Instances.Num()>=64)return EAetherBuffResult::Capacity;
        FAetherBuffInstance I;I.InstanceId=Delivery.IsValid()?Delivery:FGuid::NewGuid();I.LifeId=LifeId;
        I.Definition=D;I.InstigatorId=Instigator;I.Sources.Add(Source,1);I.StartedAt=Now;I.ExpiresAt=Now+D.Duration;
        I.NextTickAt=D.Period>0?Now+D.Period:0;Instances.Add(MoveTemp(I));
    }
    if(Delivery.IsValid())Deliveries.Add(Delivery);
    ++Revision;ResolveSuppression();return Result;
}
void FAetherBuffState::ResolveSuppression()
{
    for(auto& I:Instances)
    {
        I.bSuppressed=false;if(I.Definition.ExclusiveGroup.IsEmpty())continue;
        for(const auto& Other:Instances)if(Other.InstanceId!=I.InstanceId&&Other.Definition.ExclusiveGroup==I.Definition.ExclusiveGroup&&
            (Other.Definition.Priority>I.Definition.Priority||
             (Other.Definition.Priority==I.Definition.Priority&&Other.InstanceId.ToString()<I.InstanceId.ToString())))
        {I.bSuppressed=true;break;}
    }
}
bool FAetherBuffState::RemoveSource(const FString& Source)
{
    bool Changed=false;for(auto& I:Instances)if(I.Sources.Remove(Source)){++I.Revision;Changed=true;}
    Instances.RemoveAll([](const auto& I){return I.Sources.IsEmpty();});
    if(Changed){++Revision;ResolveSuppression();}return Changed;
}
bool FAetherBuffState::Dispel(const FString& Tag)
{
    const int32 Removed=Instances.RemoveAll([&](const auto& I){return I.Definition.bDispellable&&I.Definition.DispelTags.Contains(Tag);});
    if(Removed){++Revision;ResolveSuppression();}return Removed>0;
}
bool FAetherBuffState::NextDue(double Now,FAetherBuffDueEvent& Event)
{
    int32 Selected=INDEX_NONE;double Time=MAX_dbl;bool Expiry=false;
    for(int32 Index=0;Index<Instances.Num();++Index)
    {
        const auto& I=Instances[Index];const bool Tick=I.NextTickAt>0&&I.NextTickAt<I.ExpiresAt;
        const double Due=Tick?I.NextTickAt:I.ExpiresAt;
        if(Due<=Now&&(Due<Time||(Due==Time&&(Selected==INDEX_NONE||I.InstanceId.ToString()<Instances[Selected].InstanceId.ToString()))))
        {Selected=Index;Time=Due;Expiry=!Tick;}
    }
    if(Selected==INDEX_NONE)return false;
    auto& I=Instances[Selected];Event={};Event.InstanceId=I.InstanceId;Event.Time=Time;Event.bExpiry=Expiry;Event.Stacks=I.Stacks();
    if(Expiry){Instances.RemoveAt(Selected);ResolveSuppression();}
    else
    {
        Event.TickIndex=++I.TickIndex;I.NextTickAt+=I.Definition.Period;
        if(!I.bSuppressed)Event.Operations=I.Definition.Operations;
    }
    ++Revision;return true;
}
bool FAetherBuffState::HasTag(const FString& Tag) const
{
    for(const auto& I:Instances)if(!I.bSuppressed)for(const auto& O:I.Definition.Operations)
        if(O.Kind==EAetherBuffOperation::Tag&&O.Id==Tag)return true;
    return false;
}
TArray<FAetherAttributeContribution> FAetherBuffState::Attributes() const
{
    TArray<FAetherAttributeContribution> Out;
    for(const auto& I:Instances)if(!I.bSuppressed)for(const auto& O:I.Definition.Operations)if(O.Kind==EAetherBuffOperation::Attribute)
    {
        const double Value=O.AttributeOperation==EAetherAttributeOperation::Multiply?FMath::Pow(O.Value,I.Stacks()):
            O.AttributeOperation==EAetherAttributeOperation::Override?O.Value:O.Value*I.Stacks();
        Out.Add({O.Id,TEXT("Buff.")+I.InstanceId.ToString(EGuidFormats::Digits),O.AttributeOperation,Value,I.Definition.Priority});
    }
    return Out;
}
TArray<FAetherExternalSkillGrant> FAetherBuffState::SkillGrants() const
{
    TArray<FAetherExternalSkillGrant> Out;
    for(const auto& I:Instances)if(!I.bSuppressed)for(const auto& O:I.Definition.Operations)if(O.Kind==EAetherBuffOperation::Skill)
        Out.Add({TEXT("Buff.")+I.InstanceId.ToString(EGuidFormats::Digits),O.Id,int32(O.Value),EAetherSkillGrantSource::Temporary});
    return Out;
}
