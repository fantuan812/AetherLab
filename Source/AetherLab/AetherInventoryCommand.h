#pragma once
#include "CoreMinimal.h"
#include "AetherInventoryCommand.generated.h"
UENUM()
enum class EAetherInventoryResult : uint8 { Applied, InvalidCommand, StaleRevision, MissingInstance, NotAllowed, Capacity, InsufficientFunds, NotReady, OutOfReach, StorageUnavailable, CommandConflict };
USTRUCT()
struct FAetherInventoryCommand
{
 GENERATED_BODY()
 UPROPERTY() FGuid CommandId;
 // Profile.Revision is a conservative inventory transaction version.
 UPROPERTY() int32 ExpectedInventoryRevision=-1;
 UPROPERTY() FName Action;
 UPROPERTY() FGuid ItemInstanceId;
 UPROPERTY() FGuid DestinationInstanceId;
 UPROPERTY() int32 Quantity=1;
 UPROPERTY() FName DefinitionId;
 UPROPERTY() FName ShopId;
 bool SameRequest(const FAetherInventoryCommand& Other) const;
};
USTRUCT()
struct FAetherInventoryReceipt
{
 GENERATED_BODY()
 UPROPERTY() FAetherInventoryCommand Command;
 UPROPERTY() int32 FinalRevision=0;
 UPROPERTY() int32 Transferred=0;
};
struct FAetherProfile;struct FAetherRules;struct FAetherUseRule;
namespace AetherItems
{
 EAetherInventoryResult Prepare(FAetherProfile& Candidate,const FAetherInventoryCommand& Command,const FAetherRules& Rules,int32& Transferred);
 bool Grant(FAetherProfile& Candidate,const TMap<FName,int32>& Items,int32 Gold,const FAetherRules& Rules);
 bool GrantTable(FAetherProfile& Candidate,FName Table,const FAetherRules& Rules);
 const FAetherUseRule* Use(const FAetherProfile& Profile,FGuid Instance,const FAetherRules& Rules);
}
