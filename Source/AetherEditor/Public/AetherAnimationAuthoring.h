#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AetherAnimationAuthoring.generated.h"
class UAnimSequence;
class UAnimBlueprint;
// 编辑器专用：将可审阅的姿态关键帧配方烘焙为原生 AnimSequence，游戏运行不读配方。
UCLASS()
class AETHEREDITOR_API UAetherAnimationAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // 将运行时图入口接入正式蓝图输出；重复作者时复用已有入口。
    UFUNCTION(BlueprintCallable,Category="Aether|Authoring")
    static bool ConnectNativePose(UAnimBlueprint* Blueprint,bool SourcePose,FString& Reason);
    UFUNCTION(BlueprintCallable,Category="Aether|Authoring")
    static UAnimSequence* BakeControlledClip(UAnimSequence* Source,const FString& Recipe,const FString& PackagePath,FString& Reason);
};
