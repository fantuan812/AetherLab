#include "Skills/AetherSkillHotbarCell.h"
#include "Skills/AetherSkillDragDrop.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "InputCoreTypes.h"

class SAetherSkillHotbarCell : public SBorder
{
public:
    SLATE_BEGIN_ARGS(SAetherSkillHotbarCell){} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {SBorder::Construct(SBorder::FArguments().Padding(10)[SAssignNew(Text,STextBlock).AutoWrapText(true)]);}
    void Present(const FAetherInspectRequest& R,int32 Index,const FString& Label){Request=R;Slot=Index;Text->SetText(FText::FromString(Label));}
    FOnAetherHotbarBind OnBind;FOnAetherHotbarInspect OnInspect;
    virtual bool SupportsKeyboardFocus() const override{return true;}
    virtual FReply OnDrop(const FGeometry&,const FDragDropEvent& E) override
    {
        const auto Drag=E.GetOperationAs<FAetherSkillDragDrop>();
        if(!Drag||!Drag->Request.Context.Same(Request.Context))return FReply::Unhandled();
        OnBind.ExecuteIfBound(Drag->Request,Slot);return FReply::Handled();
    }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& E) override
    {
        if(E.GetEffectingButton()==EKeys::LeftMouseButton||E.GetEffectingButton()==EKeys::RightMouseButton)
        {OnInspect.ExecuteIfBound(Request);return FReply::Handled().SetUserFocus(SharedThis(this));}
        return FReply::Unhandled();
    }
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent& E) override
    {
        if(E.GetKey()==EKeys::Enter||E.GetKey()==EKeys::Gamepad_FaceButton_Bottom){OnInspect.ExecuteIfBound(Request);return FReply::Handled();}
        return FReply::Unhandled();
    }
private:
    FAetherInspectRequest Request;int32 Slot=0;TSharedPtr<STextBlock> Text;
};
TSharedRef<SWidget> UAetherSkillHotbarCell::RebuildWidget()
{
    Cell=SNew(SAetherSkillHotbarCell);Cell->OnBind=FOnAetherHotbarBind::CreateWeakLambda(this,[this](const auto& R,int32 Index){OnBind.ExecuteIfBound(R,Index);});
    Cell->OnInspect=FOnAetherHotbarInspect::CreateWeakLambda(this,[this](const auto& R){OnInspect.ExecuteIfBound(R);});
    Cell->Present(Request,Slot,Label);return Cell.ToSharedRef();
}
void UAetherSkillHotbarCell::Present(FAetherInspectRequest R,int32 Index,FString Text)
{Request=MoveTemp(R);Slot=Index;Label=MoveTemp(Text);if(Cell)Cell->Present(Request,Slot,Label);}
void UAetherSkillHotbarCell::ReleaseSlateResources(bool Children)
{Super::ReleaseSlateResources(Children);Cell.Reset();}
