#pragma once
#include "Blueprint/UserWidget.h"
#include "Skills/AetherSkillTreeModel.h"
#include "AetherSkillNodeWidget.generated.h"
class UImage;
class UTextBlock;
UCLASS()
class AETHERUI_API UAetherSkillNodeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetNode(const FAetherSkillTreeNode& Value);
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    void RefreshNode();
    FAetherSkillTreeNode Node;
    FString LoadedIcon;
    uint32 IconGeneration=0;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> SkillIcon;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SkillTitle;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SkillState;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SkillSource;
};
