#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/Object.h"
#include "AetherInputProfile.generated.h"
UCLASS(Config=Input)
class AETHERLAB_API UAetherInputProfile : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config) TMap<FName,FKey> Keys;
    static uint32 Context(FName Action)
    {
        if(Action=="Tab")return 3;
        if(Action=="Delete")return 17;
        if(Action=="B"||Action=="N"||Action=="Seven"||Action=="Eight")return 1;
        if(Action=="Claim")return 2;
        if(Action=="Invite"||Action=="AcceptInvite"||Action=="LeaveParty"||Action=="H")return 16;
        if(Action=="I"||Action=="J"||Action=="K"||Action=="M"||Action=="P"||Action=="Escape")return 63;
        return 32;
    }
};
