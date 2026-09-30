#pragma once
#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
// 唯一当前契约。目录整体解析一次；修改定义必须重启进程，不能半途切换绑定。
struct FAetherMotionBindingChain {FName Name,SourceStart,SourceEnd,TargetStart,TargetEnd;};
struct FAetherMotionBindingAlignment {FName SourceBone,SourceChild,TargetBone,TargetChild;};
struct AETHERMOTIONRUNTIME_API FAetherMotionBinding
{
 FString Id,TargetMesh,SourceRig,TargetRig,Forward,Reverse,Profile,SourceAnimationClass;
 FName SourceRoot,TargetPelvis,TargetRoot,TargetHead;
 double SourceHeading=0;bool Configured=false;
 TArray<FAetherMotionBindingChain> Chains;TArray<FAetherMotionBindingAlignment> Alignments;TMap<FName,FString> Styles;
 FString AnimationClass,PreviewIdle,WalkAnimation,AttackAnimation;
 TMap<FName,FSoftObjectPath> AnimationAssets;TArray<FSoftObjectPath> LightAnimations;
 TArray<FString> CharacterDefinitions;
 TArray<FSoftObjectPath> RuntimeAssets() const;
};
namespace AetherMotionBindings
{
 AETHERMOTIONRUNTIME_API FString DefinitionPath();
 AETHERMOTIONRUNTIME_API bool Parse(const FString& Text,TArray<FAetherMotionBinding>& Out,FString& Reason);
 AETHERMOTIONRUNTIME_API bool Load(const FString& Path,TArray<FAetherMotionBinding>& Out,FString& Reason);
 AETHERMOTIONRUNTIME_API const TArray<FAetherMotionBinding>& All(FString& Reason);
 AETHERMOTIONRUNTIME_API const FAetherMotionBinding* ForMesh(const FSoftObjectPath& Mesh,FString& Reason);
}
