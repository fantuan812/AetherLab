#pragma once
#include "CoreMinimal.h"
struct FAetherProfile;
class AAetherFrontierCharacter;
class AAetherFrontierProp;
struct FAetherGuidance
{
 int32 Quest=INDEX_NONE;
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
 int32 SelectQuest(const FAetherProfile& Profile,int32 Preferred,bool Cycle=false);
 FString ObjectiveLabel(FName Id);
 FAetherGuidance Resolve(AAetherFrontierCharacter* Character);
 FAetherInteractionTarget SelectInteraction(AAetherFrontierCharacter* Character);
 bool IsPersonalFire(FName Service);
 bool CanInspectFire(const AAetherFrontierProp* Fire);
}
