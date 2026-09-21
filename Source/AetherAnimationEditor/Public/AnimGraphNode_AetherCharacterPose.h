#pragma once
#include "AnimGraphNode_Base.h"
#include "Animation/AnimNode_AetherCharacterPose.h"
#include "AnimGraphNode_AetherCharacterPose.generated.h"
UCLASS()
class AETHERANIMATIONEDITOR_API UAnimGraphNode_AetherCharacterPose : public UAnimGraphNode_Base
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,Category=Settings) FAnimNode_AetherCharacterPose Node;
    virtual FText GetNodeTitle(ENodeTitleType::Type) const override{return FText::FromString(TEXT("Aether Character Pose"));}
    virtual FText GetTooltipText() const override{return FText::FromString(TEXT("执行原生移动、生成混合、受控动作及手足 IK 图；必须连接 Output Pose。"));}
    virtual FString GetNodeCategory() const override{return TEXT("Aether Animation");}
};
