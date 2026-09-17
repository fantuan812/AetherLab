#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AetherWorldAuthoring.generated.h"
UCLASS()
class UAetherWorldAuthoring:public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable,Category="Aether|Authoring") static bool BakeStaticShell(UWorld* World);
 static bool IsShellPiece(FName Id);
};
