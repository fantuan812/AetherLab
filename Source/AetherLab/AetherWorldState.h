#pragma once
#include "CoreMinimal.h"
#include "ReactiveTypes.h"
class AAetherFrontierProp;
namespace AetherWorldState
{
 FName RegionFor(FVector Position);
 bool Merge(TArray<FReactiveSaveRecord>& Repository,const TArray<FReactiveSaveRecord>& Loaded);
}
struct FAetherEntityRegistry
{
 TMap<FName,TWeakObjectPtr<AAetherFrontierProp>> Loaded;
 bool Register(FName Id,AAetherFrontierProp* Actor);
 void Remove(FName Id){Loaded.Remove(Id);}
 AAetherFrontierProp* Find(FName Id) const;
};
