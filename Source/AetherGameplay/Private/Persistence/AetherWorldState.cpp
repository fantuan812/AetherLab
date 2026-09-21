#include "Persistence/AetherWorldState.h"
#include "Framework/AetherFrontier.h"
FName AetherWorldState::RegionFor(FVector P)
{return *FString::Printf(TEXT("%d_%d"),FMath::FloorToInt(P.X/7000),FMath::FloorToInt(P.Y/7000));}
bool AetherWorldState::Merge(TArray<FReactiveSaveRecord>& Repository,const TArray<FReactiveSaveRecord>& Loaded)
{
 auto Next=Repository;TSet<FName> Seen;
 for(const auto& R:Next)if(R.StableId.IsNone()||Seen.Contains(R.StableId))return false;else Seen.Add(R.StableId);
 Seen.Reset();
 for(auto R:Loaded)
 {
  if(R.StableId.IsNone()||Seen.Contains(R.StableId)||!R.Transform.IsValid())return false;Seen.Add(R.StableId);
  R.RegionId=RegionFor(R.Transform.GetLocation());
  if(auto* Old=Next.FindByPredicate([&](const auto& V){return V.StableId==R.StableId;}))*Old=R;else Next.Add(R);
 }
 Next.Sort([](const auto& A,const auto& B){return A.StableId.LexicalLess(B.StableId);});Repository=MoveTemp(Next);return true;
}
bool FAetherEntityRegistry::Register(FName Id,AAetherFrontierProp* A)
{if(Id.IsNone()||!IsValid(A)||Find(Id))return false;Loaded.Add(Id,A);return true;}
AAetherFrontierProp* FAetherEntityRegistry::Find(FName Id) const
{const auto* A=Loaded.Find(Id);return A?A->Get():nullptr;}
