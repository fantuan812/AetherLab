#pragma once
#include "Components/Widget.h"
#include "Skills/AetherSkillTreeModel.h"
#include "AetherSkillGraphWidget.generated.h"
class SAetherSkillGraph;
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAetherSkillNodeSelected,const FAetherSkillNodeIdentity&,bool);

// 专用图形控件：真实节点/连线/命中区域，不把技能树拼成一段 TextBlock。
UCLASS()
class AETHERUI_API UAetherSkillGraphWidget : public UWidget
{
    GENERATED_BODY()
public:
    void SetModel(const FAetherSkillTreeModel& Model);
    void SelectNode(const FAetherSkillNodeIdentity& Node);
    void ResetView();
    void CancelInteraction();
    FOnAetherSkillNodeSelected OnNodeSelected;
    virtual void ReleaseSlateResources(bool ReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    FAetherSkillTreeModel Model;
    TSharedPtr<SAetherSkillGraph> Graph;
};
