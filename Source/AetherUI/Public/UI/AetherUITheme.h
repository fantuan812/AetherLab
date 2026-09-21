#pragma once
#include "Engine/DataAsset.h"
#include "AetherUITheme.generated.h"
UCLASS(BlueprintType)
class AETHERUI_API UAetherUITheme : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor Panel=FLinearColor(.025,.035,.05,.98);
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor Card=FLinearColor(.045,.065,.09,1);
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor Text=FLinearColor(.86,.9,.95,1);
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor Accent=FLinearColor(1,.82,.45,1);
    UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="12",ClampMax="26")) int32 BodySize=16;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0",ClampMax="24")) float Padding=12;
    static const UAetherUITheme& Get();
};
