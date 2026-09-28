#pragma once
#include "CoreMinimal.h"
#include "Profile/AetherProfileState.h"

enum class EAetherAttributeOperation:uint8 { Add, Percent, Multiply, Override };
struct FAetherAttributeContribution
{
    FString AttributeId, SourceId;
    EAetherAttributeOperation Operation=EAetherAttributeOperation::Add;
    double Value=0;
    int32 Priority=0;
};
struct FAetherResolvedAttributes
{
    TMap<FString,double> Values;
    TArray<FAetherAttributeContribution> Contributions;
};
namespace AetherAttributes
{
    AETHERCORE_API bool IsDerived(const FString& Id);
    AETHERCORE_API bool Resolve(const TArray<FAetherAttributeContribution>& Sources,FAetherResolvedAttributes& Out,FString& Reason);
    AETHERCORE_API bool ResolveProfile(const FAetherProfileStateV10& Profile,const FAetherV10ItemDefinitions& Items,
        const FAetherSkillDefinitionsV10& Skills,const TArray<FAetherExternalSkillGrant>& Grants,
        const TArray<FAetherAttributeContribution>& Additional,FAetherResolvedAttributes& Out,FString& Reason);
}
