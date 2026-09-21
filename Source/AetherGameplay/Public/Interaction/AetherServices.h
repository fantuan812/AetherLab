#pragma once
#include "CoreMinimal.h"
#include "AetherServices.generated.h"

UENUM()
enum class EAetherServiceResult : uint8
{
    Committed, AlreadyProcessed, InvalidCommand, Unauthorized, TargetChanged,
    StaleProfile, InsufficientPower, Busy, StorageFailure
};

// ID is retained across retries; expected revision prevents re-execution after receipt eviction.
USTRUCT()
struct FAetherWorldServiceCommand
{
    GENERATED_BODY()
    UPROPERTY() FGuid Id;
    UPROPERTY() FName TargetId;
    UPROPERTY() int32 ExpectedRevision=0;
    bool Matches(const FAetherWorldServiceCommand& Other) const
    {return Id==Other.Id&&TargetId==Other.TargetId&&ExpectedRevision==Other.ExpectedRevision;}
};
USTRUCT()
struct FAetherWorldServiceReceipt
{
    GENERATED_BODY()
    UPROPERTY() FAetherWorldServiceCommand Command;
    UPROPERTY() FString CharacterId;
};
namespace AetherServices
{
    bool IsService(FName Service);
    FString Message(EAetherServiceResult Result);
}
