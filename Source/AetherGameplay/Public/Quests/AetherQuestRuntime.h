#pragma once
#include "CoreMinimal.h"
#include "Framework/AetherRules.h"
#include "AetherQuestRuntime.generated.h"
struct FAetherProfile;
// Only the authority composition root supplies proven source IDs. This ledger never pays rewards.
USTRUCT()
struct FAetherWorldFacts
{
 GENERATED_BODY()
 UPROPERTY() TMap<FName,FName> Sources;
 bool Record(FName Objective,FName Source,const FAetherRules& Rules=FAetherRules::Get());
 bool Validate(const FAetherRules& Rules=FAetherRules::Get()) const;
 bool Allows(FName Objective,const FAetherRules& Rules=FAetherRules::Get()) const;
};
namespace AetherQuests
{
 bool Available(const FAetherProfile& P,FName Quest,const FAetherRules& R);
 bool Complete(const FAetherProfile& P,FName Quest,const FAetherRules& R);
 bool Observe(FAetherProfile& P,FName Fact,const FAetherRules& R);
 bool Claim(FAetherProfile& P,FName Quest,const FAetherRules& R);
 bool Settle(FAetherProfile& P,const FAetherWorldFacts& Facts,bool Manual,const FAetherRules& R=FAetherRules::Get());
}
