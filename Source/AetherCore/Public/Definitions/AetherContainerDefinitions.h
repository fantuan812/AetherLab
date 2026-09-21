#pragma once
#include "World/AetherContainerState.h"
struct FAetherStaticContainerDefinition
{
    FString Id,Label,Region;
    EAetherContainerKind Kind=EAetherContainerKind::SharedChest;
    FVector Location=FVector::ZeroVector;
    int32 Capacity=32;
};
/** 静态容器只声明首次创建模板；已有持久实例不能被作者位置或容量覆盖。 */
struct AETHERCORE_API FAetherContainerDefinitions
{
    TArray<FAetherStaticContainerDefinition> Containers;
    bool bValid=false;
    FString Error;
    static FAetherContainerDefinitions Parse(const FString& Json);
};
