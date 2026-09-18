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
 AETHERLAB_API FName SelectQuest(const FAetherProfile& Profile,FName Preferred,bool Cycle=false);
 AETHERLAB_API FString ObjectiveLabel(FName Id);
 AETHERLAB_API FAetherGuidance Resolve(AAetherFrontierCharacter* Character);
 AETHERLAB_API FAetherInteractionTarget SelectInteraction(AAetherFrontierCharacter* Character);
 AETHERLAB_API bool IsPersonalFire(FName Service);
 AETHERLAB_API AAetherFrontierProp* SelectWaterReceiver(AAetherFrontierCharacter* Character,AAetherFrontierProp* Container);
 AETHERLAB_API bool CanInspectFire(const AAetherFrontierProp* Fire);
}
