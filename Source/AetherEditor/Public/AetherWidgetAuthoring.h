#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AetherWidgetAuthoring.generated.h"
class UWidgetBlueprint;
UCLASS()
class AETHEREDITOR_API UAetherWidgetAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // 有界、白名单的布局描述生成可在 Designer 编辑的真实 WidgetTree；业务行为仍由原生页面绑定。
    UFUNCTION(BlueprintCallable,Category="Aether|Authoring")
    static bool ApplyLayout(UWidgetBlueprint* Blueprint,const FString& Json,FString& Reason);
};
