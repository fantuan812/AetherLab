#pragma once
#include "CoreMinimal.h"
struct FAetherObjectDefinition { TSet<FName> Capabilities; double PowerW=1000,EnergyJ=3600000,ReceiverLoad=4; };
struct FAetherWorldPort { FName Id,Target;FVector From=FVector::ZeroVector,To=FVector::ZeroVector; };
struct FAetherWorldPlacement
{
 FName Id,Definition,Service,Section,Hinge;
 FVector Location=FVector::ZeroVector,Scale=FVector::OneVector;uint8 Kind=0;
 FString Label;bool bStream=false,bGlobalPower=false,bWorkshop=false,bAllowAbsentFromOlderSave=false;double Heat=0;
 TArray<FName> Supports;TArray<FAetherWorldPort> Ports;
};
struct FAetherWorldDefinitions
{
 TMap<FName,FAetherObjectDefinition> Definitions;
 TArray<FAetherWorldPlacement> Objects;
 bool bValid=false;FString Error;
 const FAetherWorldPlacement* Find(FName Id) const;
 static FAetherWorldDefinitions Parse(const FString& Text);
 static const FAetherWorldDefinitions& Get();
};
