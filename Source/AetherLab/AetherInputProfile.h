#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/Object.h"
#include "AetherInputProfile.generated.h"
UCLASS(Config=Input)
class UAetherInputProfile : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config) TMap<FName,FKey> Keys;
};
