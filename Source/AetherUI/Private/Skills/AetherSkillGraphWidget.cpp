#include "Skills/AetherSkillGraphWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"

DECLARE_DELEGATE_TwoParams(FGraphSelection,const FAetherSkillNodeIdentity&,bool);
class SAetherSkillGraph : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAetherSkillGraph){} SLATE_EVENT(FGraphSelection,OnSelected) SLATE_END_ARGS()
    void Construct(const FArguments& Args){OnSelected=Args._OnSelected;SetClipping(EWidgetClipping::ClipToBounds);}
    void SetModel(const FAetherSkillTreeModel& NewModel)
    {
        TOptional<FAetherSkillNodeIdentity> Previous;
        if(Model.Nodes.IsValidIndex(Selected))Previous=Model.Nodes[Selected].Identity;
        const bool SameOwner=Model.Context.SessionId==NewModel.Context.SessionId&&Model.Context.OwnerIdentity.Equals(NewModel.Context.OwnerIdentity,ESearchCase::CaseSensitive);
        Model=NewModel;
        Selected=SameOwner&&Previous.IsSet()?Model.Nodes.IndexOfByPredicate([&](const auto& N){return N.Identity==Previous.GetValue();}):INDEX_NONE;
        if(!Model.Nodes.IsValidIndex(Selected))Selected=Model.Nodes.IsEmpty()?INDEX_NONE:0;
        if(!SameOwner){Pan=FVector2D::ZeroVector;Zoom=1;bPanning=false;}
        SetToolTipText(FText::GetEmpty());Invalidate(EInvalidateWidgetReason::Paint);
    }
    void SelectNode(const FAetherSkillNodeIdentity& Node)
    {
        const int32 Index=Model.Nodes.IndexOfByPredicate([&](const auto& N){return N.Identity==Node;});
        if(Index!=INDEX_NONE){Selected=Index;CenterSelection();Invalidate(EInvalidateWidgetReason::Paint);}
    }
    void ResetView(){Pan=FVector2D::ZeroVector;Zoom=1;Invalidate(EInvalidateWidgetReason::Paint);}
    void CancelInteraction(){bPanning=false;SetToolTipText(FText::GetEmpty());}
    virtual bool SupportsKeyboardFocus() const override{return true;}
    virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D(720,580);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool) const override
    {
        const auto* Brush=FCoreStyle::Get().GetBrush("WhiteBrush");const FVector2D View=G.GetLocalSize();
        FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),Brush,ESlateDrawEffect::None,FLinearColor(.018f,.025f,.04f,1));
        const auto T=Transform(View);
        auto Line=[&](const TArray<FVector2D>& Points,FLinearColor Color,float Width,int32 L)
        {FSlateDrawElement::MakeLines(Out,L,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width);};
        for(const auto& E:Model.Edges)if(Model.Nodes.IsValidIndex(E.From)&&Model.Nodes.IsValidIndex(E.To))
        {
            const auto& A=Model.Nodes[E.From];const auto& B=Model.Nodes[E.To];
            const FVector2D Start=T.Offset+(A.Position+FVector2D(160,54))*T.Scale,End=T.Offset+(B.Position+FVector2D(0,54))*T.Scale;
            const double Mid=(Start.X+End.X)*.5;
            Line({Start,FVector2D(Mid,Start.Y),FVector2D(Mid,End.Y),End},A.bPermanent?FLinearColor(.4f,.7f,.5f):FLinearColor(.24f,.28f,.35f),2,Layer+1);
            Line({End+FVector2D(-7,-4)*T.Scale,End,End+FVector2D(-7,4)*T.Scale},FLinearColor(.6f,.65f,.75f),1,Layer+1);
        }
        for(int32 I=0;I<Model.Nodes.Num();++I)
        {
            const auto& N=Model.Nodes[I];const FVector2D P=T.Offset+N.Position*T.Scale,Size=FVector2D(160,108)*T.Scale;
            const FLinearColor Element=N.Mechanic==EAetherSkillMechanic::Fire?FLinearColor(1,.4f,.2f):
                N.Mechanic==EAetherSkillMechanic::Water?FLinearColor(.25f,.65f,1):N.Mechanic==EAetherSkillMechanic::Frost?FLinearColor(.6f,.9f,1):FLinearColor(.8f,.65f,1);
            const FLinearColor Border=I==Selected?FLinearColor(1,.83f,.4f):N.bAuthorized?Element:FLinearColor(.32f,.35f,.42f);
            FSlateDrawElement::MakeBox(Out,Layer+2,G.ToPaintGeometry(Size,FSlateLayoutTransform(P)),Brush,ESlateDrawEffect::None,Border);
            FSlateDrawElement::MakeBox(Out,Layer+3,G.ToPaintGeometry(Size-FVector2D(4,4),FSlateLayoutTransform(P+FVector2D(2,2))),Brush,ESlateDrawEffect::None,FLinearColor(.055f,.07f,.1f));
            const FVector2D Icon=P+FVector2D(22,24)*T.Scale;
            auto IconLine=[&](TArray<FVector2D> Points)
            {for(auto& V:Points)V=Icon+V*T.Scale;Line(Points,Element,2,Layer+4);};
            // 基础几何占位图标，不依赖第三方贴图；正式美术可替换而不改变节点身份。
            switch(N.Mechanic)
            {
            case EAetherSkillMechanic::Fire:IconLine({{0,18},{-11,8},{-6,-4},{0,-18},{4,-3},{11,6},{8,16},{0,18}});break;
            case EAetherSkillMechanic::Water:IconLine({{0,-18},{-10,3},{-9,13},{0,18},{9,13},{10,3},{0,-18}});break;
            case EAetherSkillMechanic::Frost:IconLine({{-13,-13},{13,13}});IconLine({{-13,13},{13,-13}});IconLine({{-17,0},{17,0}});IconLine({{0,-17},{0,17}});break;
            default:IconLine({{5,-18},{-10,3},{0,3},{-5,18},{12,-4},{3,-4},{5,-18}});break;
            }
            auto Text=[&](const FString& Value,FVector2D At,int32 Point,FLinearColor Color)
            {
                const auto Font=FCoreStyle::GetDefaultFontStyle("Regular",FMath::Max(8,FMath::RoundToInt(Point*T.Scale)));
                FSlateDrawElement::MakeText(Out,Layer+5,G.ToPaintGeometry(FVector2D::ZeroVector,FSlateLayoutTransform(P+At*T.Scale)),Value,Font,ESlateDrawEffect::None,Color);
            };
            Text(N.Title+FString::Printf(TEXT(" %d"),N.Identity.Rank),FVector2D(46,17),13,FLinearColor::White);
            Text(AetherSkillTree::StateLabel(N.State),FVector2D(12,62),12,Border);
            Text(N.bPermanent?TEXT("✓ 永久授权"):N.bAuthorized?TEXT("◇ 外部授权"):TEXT("○ 未学习"),FVector2D(12,83),10,FLinearColor(.7f,.76f,.84f));
        }
        return Layer+5;
    }
    virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override
    {
        const FVector2D P=G.AbsoluteToLocal(E.GetScreenSpacePosition());const int32 Hit=HitNode(G,P);
        if((E.GetEffectingButton()==EKeys::LeftMouseButton||E.GetEffectingButton()==EKeys::RightMouseButton)&&Hit!=INDEX_NONE)
        {const bool Detail=E.GetEffectingButton()==EKeys::RightMouseButton;Selected=Hit;OnSelected.ExecuteIfBound(Model.Nodes[Hit].Identity,Detail);Invalidate(EInvalidateWidgetReason::Paint);return Detail?FReply::Handled():FReply::Handled().SetUserFocus(SharedThis(this));}
        if(E.GetEffectingButton()==EKeys::MiddleMouseButton||E.GetEffectingButton()==EKeys::LeftMouseButton)
        {bPanning=true;return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));}
        return E.GetEffectingButton()==EKeys::RightMouseButton?FReply::Handled():FReply::Unhandled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent& E) override
    {
        if(bPanning&&(E.GetEffectingButton()==EKeys::MiddleMouseButton||E.GetEffectingButton()==EKeys::LeftMouseButton))
        {bPanning=false;return FReply::Handled().ReleaseMouseCapture();}return FReply::Unhandled();
    }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override{bPanning=false;}
    virtual FReply OnMouseMove(const FGeometry& G,const FPointerEvent& E) override
    {
        if(bPanning)
        {
            Pan+=G.AbsoluteToLocal(E.GetScreenSpacePosition())-G.AbsoluteToLocal(E.GetLastScreenSpacePosition());ClampPan(G.GetLocalSize());
            Invalidate(EInvalidateWidgetReason::Paint);return FReply::Handled();
        }
        const int32 Hit=HitNode(G,G.AbsoluteToLocal(E.GetScreenSpacePosition()));
        SetToolTipText(Hit==INDEX_NONE?FText::GetEmpty():FText::FromString(Model.Nodes[Hit].Title+TEXT("\n")+Model.Nodes[Hit].Reason));
        return FReply::Unhandled();
    }
    virtual FReply OnMouseWheel(const FGeometry& G,const FPointerEvent& E) override
    {
        const auto Before=Transform(G.GetLocalSize());const auto Mouse=G.AbsoluteToLocal(E.GetScreenSpacePosition());
        const auto World=(Mouse-Before.Offset)/Before.Scale;
        Zoom=FMath::Clamp(Zoom*FMath::Pow(1.15f,E.GetWheelDelta()),.7f,3.f);
        const auto After=Transform(G.GetLocalSize());Pan+=Mouse-(After.Offset+World*After.Scale);ClampPan(G.GetLocalSize());
        Invalidate(EInvalidateWidgetReason::Paint);return FReply::Handled();
    }
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent& E) override
    {
        const FKey Key=E.GetKey();FVector2D Direction=FVector2D::ZeroVector;
        if(Key==EKeys::Left||Key==EKeys::Gamepad_DPad_Left)Direction.X=-1;
        if(Key==EKeys::Right||Key==EKeys::Gamepad_DPad_Right)Direction.X=1;
        if(Key==EKeys::Up||Key==EKeys::Gamepad_DPad_Up)Direction.Y=-1;
        if(Key==EKeys::Down||Key==EKeys::Gamepad_DPad_Down)Direction.Y=1;
        if(!Direction.IsNearlyZero())
        {
            Selected=AetherSkillTree::Navigate(Model,Selected,Direction);CenterSelection();
            if(Model.Nodes.IsValidIndex(Selected))OnSelected.ExecuteIfBound(Model.Nodes[Selected].Identity,false);
            return FReply::Handled();
        }
        if(Key==EKeys::Enter||Key==EKeys::SpaceBar||Key==EKeys::Gamepad_FaceButton_Bottom)
        {if(!E.IsRepeat()&&Model.Nodes.IsValidIndex(Selected))OnSelected.ExecuteIfBound(Model.Nodes[Selected].Identity,true);return FReply::Handled();}
        if(Key==EKeys::Home||Key==EKeys::Gamepad_FaceButton_Top){ResetView();return FReply::Handled();}
        return FReply::Unhandled();
    }
private:
    struct FView {FVector2D Offset;double Scale=1;};
    FBox2D Bounds() const
    {
        FBox2D B(ForceInit);for(const auto& N:Model.Nodes){B+=N.Position;B+=N.Position+FVector2D(160,108);}
        return B.bIsValid?B:FBox2D(FVector2D::ZeroVector,FVector2D(720,580));
    }
    FView Transform(FVector2D View) const
    {
        const auto B=Bounds();const auto Size=B.GetSize()+FVector2D(40,40);
        const double Fit=FMath::Max(.05,FMath::Min(double(View.X)/Size.X,double(View.Y)/Size.Y));
        const double Scale=Fit*Zoom;return {View*.5-B.GetCenter()*Scale+Pan,Scale};
    }
    int32 HitNode(const FGeometry& G,FVector2D P) const
    {
        const auto T=Transform(G.GetLocalSize());P=(P-T.Offset)/T.Scale;
        for(int32 I=Model.Nodes.Num()-1;I>=0;--I)
        {const auto Start=Model.Nodes[I].Position;if(P.X>=Start.X&&P.X<=Start.X+160&&P.Y>=Start.Y&&P.Y<=Start.Y+108)return I;}
        return INDEX_NONE;
    }
    void ClampPan(FVector2D View)
    {
        const auto T=Transform(View);const auto Extent=Bounds().GetExtent()*T.Scale+View*.35;
        Pan.X=FMath::Clamp(Pan.X,-Extent.X,Extent.X);Pan.Y=FMath::Clamp(Pan.Y,-Extent.Y,Extent.Y);
    }
    void CenterSelection()
    {
        if(!Model.Nodes.IsValidIndex(Selected))return;const auto View=GetCachedGeometry().GetLocalSize();const auto T=Transform(View);
        const auto P=T.Offset+(Model.Nodes[Selected].Position+FVector2D(80,54))*T.Scale;
        if(P.X<80||P.X>View.X-80||P.Y<60||P.Y>View.Y-60)Pan+=View*.5-P;
        ClampPan(View);Invalidate(EInvalidateWidgetReason::Paint);
    }
    FAetherSkillTreeModel Model;FGraphSelection OnSelected;int32 Selected=INDEX_NONE;
    FVector2D Pan=FVector2D::ZeroVector;float Zoom=1;bool bPanning=false;
};
TSharedRef<SWidget> UAetherSkillGraphWidget::RebuildWidget()
{
    Graph=SNew(SAetherSkillGraph).OnSelected(FGraphSelection::CreateWeakLambda(this,[this](const auto& N,bool Detail){OnNodeSelected.Broadcast(N,Detail);}));
    Graph->SetModel(Model);return Graph.ToSharedRef();
}
void UAetherSkillGraphWidget::SetModel(const FAetherSkillTreeModel& InModel){Model=InModel;if(Graph)Graph->SetModel(Model);}
void UAetherSkillGraphWidget::SelectNode(const FAetherSkillNodeIdentity& Node){if(Graph)Graph->SelectNode(Node);}
void UAetherSkillGraphWidget::ResetView(){if(Graph)Graph->ResetView();}
void UAetherSkillGraphWidget::CancelInteraction()
{
    if(!Graph)return;Graph->CancelInteraction();
    if(Graph->HasMouseCapture()&&FSlateApplication::IsInitialized())FSlateApplication::Get().ReleaseAllPointerCapture();
}
void UAetherSkillGraphWidget::ReleaseSlateResources(bool Children){CancelInteraction();Super::ReleaseSlateResources(Children);Graph.Reset();}
