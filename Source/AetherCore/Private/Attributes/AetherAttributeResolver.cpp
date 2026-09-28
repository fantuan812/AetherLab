#include "Attributes/AetherAttributeResolver.h"

bool AetherAttributes::IsDerived(const FString& Id)
{
    static const TSet<FString> Names={TEXT("Damage"),TEXT("Posture"),TEXT("Armor"),TEXT("FireResist"),TEXT("WaterResist"),
        TEXT("FrostResist"),TEXT("StormResist"),TEXT("MaxHealth"),TEXT("MaxMana"),TEXT("MaxStamina"),TEXT("MoveSpeed"),TEXT("ActionSpeed")};
    return Names.Contains(Id);
}
bool AetherAttributes::Resolve(const TArray<FAetherAttributeContribution>& Sources,FAetherResolvedAttributes& Out,FString& Reason)
{
    FAetherResolvedAttributes Next;Next.Contributions=Sources;
    if(Sources.Num()>1024){Reason=TEXT("Attribute contribution limit exceeded");return false;}
    for(const auto& S:Sources)
        if(!IsDerived(S.AttributeId)||S.SourceId.IsEmpty()||!FMath::IsFinite(S.Value)||FMath::Abs(S.Value)>100000||
            uint8(S.Operation)>uint8(EAetherAttributeOperation::Override)||
            (S.Operation==EAetherAttributeOperation::Multiply&&(S.Value<0||S.Value>10))||
            (S.Operation==EAetherAttributeOperation::Percent&&FMath::Abs(S.Value)>10))
        {Reason=TEXT("Invalid attribute, source, operation or magnitude");return false;}
    Next.Contributions.Sort([](const auto& A,const auto& B) {
        if(A.AttributeId!=B.AttributeId)return A.AttributeId<B.AttributeId;
        if(A.SourceId!=B.SourceId)return A.SourceId<B.SourceId;
        if(A.Operation!=B.Operation)return uint8(A.Operation)<uint8(B.Operation);
        if(A.Priority!=B.Priority)return A.Priority<B.Priority;
        return A.Value<B.Value;
    });
    TSet<FString> Keys;for(const auto& S:Sources)Keys.Add(S.AttributeId);
    Keys.Add(TEXT("MoveSpeed"));Keys.Add(TEXT("ActionSpeed"));
    for(const auto& Key:Keys)
    {
        const bool Speed=Key==TEXT("MoveSpeed")||Key==TEXT("ActionSpeed");
        double Add=Speed?1.:0.,Percent=0,Multiply=1;const FAetherAttributeContribution* Override=nullptr;
        for(const auto& S:Next.Contributions)if(S.AttributeId==Key)
        {
            switch(S.Operation) {
            case EAetherAttributeOperation::Add:Add+=S.Value;break;
            case EAetherAttributeOperation::Percent:Percent+=S.Value;break;
            case EAetherAttributeOperation::Multiply:Multiply*=S.Value;break;
            case EAetherAttributeOperation::Override:
                if(!Override||S.Priority>Override->Priority)Override=&S;break;
            }
        }
        const double Value=Override?Override->Value:Add*FMath::Max(0.,1.+Percent)*Multiply;
        if(!FMath::IsFinite(Value)){Reason=TEXT("Attribute arithmetic overflow");return false;}
        Next.Values.Add(Key,FMath::Clamp(Value,Speed?.1:0.,Speed?3.:100000.));
    }
    Out=MoveTemp(Next);Reason.Reset();return true;
}
bool AetherAttributes::ResolveProfile(const FAetherProfileStateV10& P,const FAetherV10ItemDefinitions& Items,
    const FAetherSkillDefinitionsV10& Skills,const TArray<FAetherExternalSkillGrant>& Grants,
    const TArray<FAetherAttributeContribution>& Additional,FAetherResolvedAttributes& Out,FString& Reason)
{
    TArray<FAetherAttributeContribution> Sources=Additional;
    for(const auto& S:P.Inventory.EquippedStats(Items))Sources.Add({S.Key,TEXT("Equipment"),EAetherAttributeOperation::Add,S.Value});
    for(const auto& Pair:Skills.Skills)if(!Pair.Value.bActive)
        if(const auto* Effect=Skills.Effect(Pair.Key,P.Skills.EffectiveRank(Pair.Key,Grants)))
            for(const auto& S:Effect->PassiveStats)Sources.Add({S.Key,TEXT("Skill.")+Pair.Key,EAetherAttributeOperation::Add,S.Value});
    return Resolve(Sources,Out,Reason);
}
