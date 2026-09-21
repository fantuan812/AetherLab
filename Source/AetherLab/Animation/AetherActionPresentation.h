#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AetherActionPresentation.generated.h"
class UAnimSequence;
// 只复制动作身份和服务器时间；姿态资源不决定命中、物品提交或移动。
USTRUCT()
struct FAetherActionPresentation
{
    GENERATED_BODY()
    UPROPERTY() FName Id;
    UPROPERTY() uint32 Serial=0;
    UPROPERTY() float StartedAt=0;
    UPROPERTY() float Duration=0;
    UPROPERTY() bool bHasContact=false;
    UPROPERTY() FVector Contact=FVector::ZeroVector;
};
UCLASS(BlueprintType)
class AETHERLAB_API UAetherActionSet : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TMap<FName,TObjectPtr<UAnimSequence>> Clips;
};
