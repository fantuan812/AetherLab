#pragma once
#include "CoreMinimal.h"
struct FAetherMapBeacon{FName Id,RequiredQuest;FString Label;FVector Position=FVector::ZeroVector;};
struct FAetherMapRegion{FString Label;FVector2D Center=FVector2D::ZeroVector,Extent=FVector2D::ZeroVector;FLinearColor Color;};
struct AETHERCORE_API FAetherMapDefinitions
{
    TArray<FAetherMapRegion> Regions;TArray<FAetherMapBeacon> Beacons;
    bool bValid=false;FString Error;
    static FAetherMapDefinitions Parse(const FString& Json);
    static const FAetherMapDefinitions& Get();
};
