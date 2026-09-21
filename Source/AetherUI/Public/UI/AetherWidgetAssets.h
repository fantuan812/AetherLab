#pragma once
#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
class UUserWidget;
class UWidgetTree;
namespace AetherWidgetAssets
{
    AETHERUI_API void BindDesigner(UUserWidget& Owner,UWidgetTree& Tree);
    AETHERUI_API void BindButton(UUserWidget& Owner,FName Widget,FName Function);
    AETHERUI_API UClass* Resolve(UClass* NativeClass,const TCHAR* Variant=nullptr);
    AETHERUI_API FSoftObjectPath Icon(const FString& StableId);
    template<typename T> TSubclassOf<T> Class(const TCHAR* Variant=nullptr){return Resolve(T::StaticClass(),Variant);}
}
