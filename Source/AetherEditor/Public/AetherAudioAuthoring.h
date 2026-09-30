#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AetherAudioAuthoring.generated.h"
class UAetherAudioCatalog;

// Editor-module-only bridge. Runtime catalog properties remain read-only in Python/BP.
UCLASS()
class AETHEREDITOR_API UAetherAudioAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Aether|Authoring")
    static bool ImportGeneratedCatalog(UAetherAudioCatalog* Catalog, const FString& Json);
    UFUNCTION(BlueprintCallable, Category="Aether|Authoring")
    static FString ExportGeneratedCatalog(const UAetherAudioCatalog* Catalog);
    UFUNCTION(BlueprintCallable, Category="Aether|Authoring")
    static bool IsCatalogRegistered(const UAetherAudioCatalog* Catalog);
};
