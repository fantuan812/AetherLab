#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AetherAnimationAuthoring.generated.h"
class UAnimSequence;
// 编辑器专用：将可审阅的姿态关键帧配方烘焙为原生 AnimSequence，游戏运行不读配方。
UCLASS()
class AETHEREDITOR_API UAetherAnimationAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Aether|Authoring")
    static UAnimSequence* BakeControlledClip(UAnimSequence* Source,const FString& Recipe,const FString& PackagePath,FString& Reason);
};
