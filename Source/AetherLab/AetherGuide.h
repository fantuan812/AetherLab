#pragma once
#include "CoreMinimal.h"
struct FAetherProfile;
class AAetherFrontierCharacter;
class AAetherFrontierProp;
struct FAetherGuidance
{
 FName Quest;
 FName Objective;
 FString Title,Label,Hint;
 FVector Position=FVector::ZeroVector;
 bool bHasTarget=false,bRewardReady=false;
};
struct FAetherInteractionTarget
{
 TWeakObjectPtr<AAetherFrontierProp> Prop;
 TWeakObjectPtr<AAetherFrontierCharacter> Rescue;
 FString Prompt;
};
namespace AetherGuide
{
 FName SelectQuest(const FAetherProfile& Profile,FName Preferred,bool Cycle=false);
 FString ObjectiveLabel(FName Id);
 FAetherGuidance Resolve(AAetherFrontierCharacter* Character);
 FAetherInteractionTarget SelectInteraction(AAetherFrontierCharacter* Character);
 bool IsPersonalFire(FName Service);
 AAetherFrontierProp* SelectWaterReceiver(AAetherFrontierCharacter* Character,AAetherFrontierProp* Container);
 bool CanInspectFire(const AAetherFrontierProp* Fire);
}
