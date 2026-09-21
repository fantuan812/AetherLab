#pragma once
#include "CoreMinimal.h"
class USaveGame;
// Storage adapter: no Character, inventory, quest, or world solver dependency.
class IAetherSnapshotStore
{
public:
 virtual ~IAetherSnapshotStore()=default;
 virtual bool Exists(const FString& Slot) const=0;
 virtual USaveGame* Load(const FString& Slot) const=0;
 virtual bool IsCommitted(const FString& Slot,int32 Generation) const=0;
 virtual bool Publish(USaveGame* Snapshot,const FString& Slot,int32 Generation,bool FailAfterData)=0;
};
TSharedRef<IAetherSnapshotStore> AetherLocalSnapshotStore();
