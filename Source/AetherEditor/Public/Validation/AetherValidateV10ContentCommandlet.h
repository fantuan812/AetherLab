#pragma once
#include "Commandlets/Commandlet.h"
#include "AetherValidateV10ContentCommandlet.generated.h"
UCLASS()
class AETHEREDITOR_API UAetherValidateV10ContentCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UAetherValidateV10ContentCommandlet();
    virtual int32 Main(const FString& Params) override;
};
