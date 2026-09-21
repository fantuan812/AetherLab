#pragma once
#include "AnimGraphNode_Base.h"
#include "AnimNode_AetherGeneratedPose.h"
#include "AnimGraphNode_AetherGeneratedPose.generated.h"
UCLASS()
class AETHERMOTIONEDITOR_API UAnimGraphNode_AetherGeneratedPose : public UAnimGraphNode_Base
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,Category=Settings) FAnimNode_AetherGeneratedPose Node;
    virtual FText GetNodeTitle(ENodeTitleType::Type) const override{return FText::FromString(TEXT("Aether Generated G1 Pose"));}
    virtual FText GetTooltipText() const override{return FText::FromString(TEXT("读取完成的不可变 G1 姿态缓存；不在动画线程调用推理。"));}
    virtual FString GetNodeCategory() const override{return TEXT("Aether Motion");}
};
