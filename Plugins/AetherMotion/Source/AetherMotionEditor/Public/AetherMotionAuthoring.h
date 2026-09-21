#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AetherMotionAuthoring.generated.h"
class USkeletalMesh;
class UAnimSequence;
class UIKRetargeter;
/** 编辑器离线作者接口。JSON 来自锁定 C ABI 的 MotionAuthor.py，绝不在游戏 Tick 调用。 */
UCLASS()
class AETHERMOTIONEDITOR_API UAetherMotionAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Aether|Motion Authoring")
    static USkeletalMesh* CreateSource(const FString& SkeletonJson,FString& Reason);
    UFUNCTION(BlueprintCallable,Category="Aether|Motion Authoring")
    static bool CreateRetargetAssets(USkeletalMesh* Source,USkeletalMesh* Target,const FString& BodyId,FString& Reason);
    UFUNCTION(BlueprintCallable,Category="Aether|Motion Authoring")
    static UAnimSequence* ImportClip(const FString& ClipJson,USkeletalMesh* Source,const FString& AssetPath,FString& Reason);
    UFUNCTION(BlueprintCallable,Category="Aether|Motion Authoring")
    static UAnimSequence* RetargetClip(UAnimSequence* Animation,USkeletalMesh* Source,USkeletalMesh* Target,UIKRetargeter* Retargeter,const FString& AssetPath,FString& Reason);
    UFUNCTION(BlueprintCallable,Category="Aether|Motion Authoring")
    static bool ExportClip(UAnimSequence* Animation,USkeletalMesh* Source,const FString& SkeletonJson,const FString& OutputJson,FString& Reason);
};
