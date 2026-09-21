#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AetherWorldCapability.generated.h"
class UReactiveBodyComponent;
UINTERFACE()
class UAetherWorldCapability : public UInterface { GENERATED_BODY() };
class IAetherWorldCapability
{
 GENERATED_BODY()
public:
 virtual bool HasWorldCapability(FName Capability) const=0;
 virtual UReactiveBodyComponent* ReactionBody() const=0;
};
namespace AetherCapabilities
{
 UReactiveBodyComponent* WaterReceiver(AActor* User,AActor* Container,double RangeCm);
}
