#pragma once
#include "CoreMinimal.h"
struct FAetherProfile;
class AActor;
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
 FName StableId,ActionId;
 int32 ProfileRevision=-1;
 bool bExecutable=false;
};
namespace AetherGuide
{
 AETHERLAB_API FName SelectQuest(const FAetherProfile& Profile,FName Preferred,bool Cycle=false);
 AETHERLAB_API FString ObjectiveLabel(FName Id);
 AETHERLAB_API FAetherGuidance Resolve(AAetherFrontierCharacter* Character);
 AETHERLAB_API FAetherInteractionTarget SelectInteraction(AAetherFrontierCharacter* Character,AActor* Previous=nullptr);
 // 检查指定实例的范围、遮挡和私有归属，绝不选择替代目标。物理提示可显示但不响应 E。
 AETHERLAB_API FAetherInteractionTarget QueryTarget(AAetherFrontierCharacter* Character,AActor* Target);
 AETHERLAB_API bool ValidateSelection(AAetherFrontierCharacter* Character,const FAetherInteractionTarget& Selection);

 AETHERLAB_API bool IsPersonalFire(FName Service);
 AETHERLAB_API AAetherFrontierProp* SelectWaterReceiver(AAetherFrontierCharacter* Character,AAetherFrontierProp* Container);
 AETHERLAB_API bool CanInspectFire(const AAetherFrontierProp* Fire);
}
