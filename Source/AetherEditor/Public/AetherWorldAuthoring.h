#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AetherWorldAuthoring.generated.h"
UCLASS()
// 仅编辑器可用：运行时只使用 AetherCore 的只读外壳定义。
class AETHEREDITOR_API UAetherWorldAuthoring:public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable,Category="Aether|Authoring") static bool BakeStaticShell(UWorld* World);
};
