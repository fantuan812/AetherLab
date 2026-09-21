#include "Quests/AetherQuestRuntime.h"
#include "Persistence/AetherProfile.h"
bool FAetherWorldFacts::Record(FName Id,FName Source,const FAetherRules& R)
{
 const auto* Rule=R.Objectives.Find(Id);
 if(!Rule||Rule->Scope!=EAetherObjectiveScope::World||!Rule->FactSources.Contains(Source)||Sources.Contains(Id)||Sources.Num()>=512)return false;
 Sources.Add(Id,Source);return true;
}
bool FAetherWorldFacts::Validate(const FAetherRules& R) const
{
 if(Sources.Num()>512)return false;
 for(const auto& Pair:Sources){const auto* Rule=R.Objectives.Find(Pair.Key);if(!Rule||Rule->Scope!=EAetherObjectiveScope::World||!Rule->FactSources.Contains(Pair.Value))return false;}
 return true;
}
bool FAetherWorldFacts::Allows(FName Id,const FAetherRules& R) const
{
 const auto* Rule=R.Objectives.Find(Id);const auto* Source=Sources.Find(Id);
 return Rule&&Source&&Rule->Scope==EAetherObjectiveScope::World&&Rule->bRetroactive&&Rule->FactSources.Contains(*Source);
}
namespace AetherQuests
{
bool Available(const FAetherProfile& P,FName Id,const FAetherRules& R)
{
 const auto* Q=R.Quest(Id);if(!R.bValid||!Q||P.Claims.Contains(Id))return false;
 for(FName Required:Q->Prerequisites)if(!P.Claims.Contains(Required))return false;return true;
}
bool Complete(const FAetherProfile& P,FName Id,const FAetherRules& R)
{
 if(!Available(P,Id,R))return false;for(FName Fact:R.Quest(Id)->Objectives)if(!P.Evidence.Contains(Fact))return false;return true;
}
bool Observe(FAetherProfile& P,FName Fact,const FAetherRules& R)
{
 if(P.Evidence.Contains(Fact)||P.Evidence.Num()>=512)return false;
 for(const auto& Q:R.Quests)if(Available(P,Q.Id,R)&&Q.Objectives.Contains(Fact)){P.Evidence.Add(Fact);return true;}return false;
}
bool Claim(FAetherProfile& P,FName Id,const FAetherRules& R)
{
 if(!Complete(P,Id,R))return false;const auto& Q=*R.Quest(Id);auto Next=P;
 if(!AetherItems::Grant(Next,Q.Items,Q.Gold,R)||Next.Experience>MAX_int32-Q.Experience)return false;
 Next.Claims.Add(Id);Next.Experience+=Q.Experience;Next.bRegistered|=Q.bBindInn;
 P=MoveTemp(Next);return true;
}
bool Settle(FAetherProfile& P,const FAetherWorldFacts& Facts,bool Manual,const FAetherRules& R)
{
 bool Changed=false;
 // Each successful pass must claim at least one new ID; at most N+1 passes including the final observation.
 for(int32 Pass=0;Pass<=R.Quests.Num();++Pass)
 {
  bool Claimed=false;
  for(const auto& Q:R.Quests)if(Available(P,Q.Id,R))
  {
   for(FName Fact:Q.Objectives)if(Facts.Allows(Fact,R))Changed|=Observe(P,Fact,R);
   if(Manual||Q.bAutoClaim){const bool Done=Claim(P,Q.Id,R);Changed|=Done;Claimed|=Done;}
  }
  if(!Claimed)break;
 }
 return Changed;
}
}
