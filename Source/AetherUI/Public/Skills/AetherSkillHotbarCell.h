#pragma once
#include "Components/Widget.h"
#include "Inspection/AetherInspectionService.h"
#include "AetherSkillHotbarCell.generated.h"
class SAetherSkillHotbarCell;
DECLARE_DELEGATE_TwoParams(FOnAetherHotbarBind,const FAetherInspectRequest&,int32);
DECLARE_DELEGATE_OneParam(FOnAetherHotbarInspect,const FAetherInspectRequest&);

UCLASS()
class AETHERUI_API UAetherSkillHotbarCell : public UWidget
{
    GENERATED_BODY()
public:
    void Present(FAetherInspectRequest Request,int32 Slot,FString Label);
    FOnAetherHotbarBind OnBind;
    FOnAetherHotbarInspect OnInspect;
    virtual void ReleaseSlateResources(bool Children) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    FAetherInspectRequest Request;
    int32 Slot=0;
    FString Label;
    TSharedPtr<SAetherSkillHotbarCell> Cell;
};
