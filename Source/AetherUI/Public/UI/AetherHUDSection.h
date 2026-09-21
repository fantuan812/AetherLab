#pragma once
#include "Blueprint/UserWidget.h"
#include "AetherHUDSection.generated.h"
class UVerticalBox;
/** 可单独编辑的 HUD 容器；内容仍由只读快照驱动。 */
UCLASS()
class AETHERUI_API UAetherHUDSection : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    UVerticalBox* GetRows(){TakeWidget();return Rows;}
private:
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UVerticalBox> Rows;
};
