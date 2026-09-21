#include "Skills/AetherSkillGraphWidget.h"
#include "Skills/AetherSkillDragDrop.h"
#include "Widgets/SPanel.h"
#include "Widgets/SNullWidget.h"
#include "Layout/Children.h"
#include "Layout/ArrangedChildren.h"
#include "Skills/AetherSkillNodeWidget.h"
#include "UI/AetherWidgetAssets.h"
#include "Blueprint/UserWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"

DECLARE_DELEGATE_TwoParams(FGraphSelection,const FAetherSkillNodeIdentity&,bool);
class SAetherSkillGraph : public SPanel
{
public:
    SAetherSkillGraph():NodeChildren(this){}
    void SetNodes(const TArray<TSharedRef<SWidget>>& Widgets)
    {
        NodeChildren.Reset();for(const auto& W:Widgets)NodeChildren.Add(W);
        Invalidate(EInvalidateWidgetReason::Layout);
    }
    virtual FChildren* GetChildren() override{return &NodeChildren;}
    virtual void OnArrangeChildren(const FGeometry& G,FArrangedChildren& Arranged) const override
    {
        const auto T=Transform(G.GetLocalSize());
        for(int32 I=0;I<NodeChildren.Num()&&I<Model.Nodes.Num();++I)
            Arranged.AddWidget(G.MakeChild(ConstCastSharedRef<SWidget>(NodeChildren.GetChildAt(I)),FVector2D(156,104),
                FSlateLayoutTransform(float(T.Scale),T.Offset+(Model.Nodes[I].Position+FVector2D(2,2))*T.Scale)));
    }
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
        SetToolTipText(FText::GetEmpty());Invalidate(EInvalidateWidgetReason::Layout);
    }
    void SelectNode(const FAetherSkillNodeIdentity& Node)
    {
        const int32 Index=Model.Nodes.IndexOfByPredicate([&](const auto& N){return N.Identity==Node;});
        if(Index!=INDEX_NONE){Selected=Index;CenterSelection();Invalidate(EInvalidateWidgetReason::Layout);}
    }
    void ResetView(){Pan=FVector2D::ZeroVector;Zoom=1;Invalidate(EInvalidateWidgetReason::Layout);}
    void CancelInteraction(){bPanning=false;SetToolTipText(FText::GetEmpty());}
    virtual bool SupportsKeyboardFocus() const override{return true;}
    virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D(720,580);}
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override
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
        }
        return SPanel::OnPaint(Args,G,Clip,Out,Layer+4,Style,Enabled);
    }
    virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override
    {
        const FVector2D P=G.AbsoluteToLocal(E.GetScreenSpacePosition());const int32 Hit=HitNode(G,P);
        if((E.GetEffectingButton()==EKeys::LeftMouseButton||E.GetEffectingButton()==EKeys::RightMouseButton)&&Hit!=INDEX_NONE)
        {const bool Detail=E.GetEffectingButton()==EKeys::RightMouseButton;Selected=Hit;OnSelected.ExecuteIfBound(Model.Nodes[Hit].Identity,Detail);Invalidate(EInvalidateWidgetReason::Layout);if(!Detail){DragContext=Model.Context;DragNode=Model.Nodes[Hit].Identity;}return Detail?FReply::Handled():FReply::Handled().SetUserFocus(SharedThis(this)).DetectDrag(SharedThis(this),EKeys::LeftMouseButton);}
        if(E.GetEffectingButton()==EKeys::MiddleMouseButton||E.GetEffectingButton()==EKeys::LeftMouseButton)
        {bPanning=true;return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));}
        return E.GetEffectingButton()==EKeys::RightMouseButton?FReply::Handled():FReply::Unhandled();
    }
    virtual FReply OnDragDetected(const FGeometry&,const FPointerEvent&) override
    {
        if(!DragNode.IsSet()||!DragContext.Same(Model.Context))return FReply::Handled();
        const auto* Node=Model.Nodes.FindByPredicate([&](const auto& N){return N.Identity==DragNode.GetValue();});
        const auto* D=Node?FAetherSkillDefinitionsV10::Get().Skills.Find(Node->Identity.SkillId):nullptr;
        if(!Node||!Node->bAuthorized||!D||!D->bActive)return FReply::Handled();
        FAetherInspectTarget T;T.Kind=EAetherInspectTarget::SkillNode;T.DefinitionId=Node->Identity.SkillId;T.SkillRank=Node->Identity.Rank;
        return FReply::Handled().BeginDragDrop(FAetherSkillDragDrop::New({DragContext,T}));
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
            Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled();
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
        Invalidate(EInvalidateWidgetReason::Layout);return FReply::Handled();
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
        // 默认至少按设计字号显示，不能为塞下八条路线把中文缩成不可读的小点。
        // 超出部分通过拖动与方向键居中访问，用户仍可手动缩小查看总览。
        const double Fit=FMath::Max(1.,FMath::Min(double(View.X)/Size.X,double(View.Y)/Size.Y));
        const double Scale=Fit*Zoom;return {FVector2D(View.X*.5-B.GetCenter().X*Scale,20-B.Min.Y*Scale)+Pan,Scale};
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
        if(!Model.Nodes.IsValidIndex(Selected))return;const auto View=GetCachedGeometry().GetLocalSize();
        // 首次 Slate 排版前尺寸为零，此时居中会把初始视图永久偏移。
        if(View.X<=0||View.Y<=0)return;const auto T=Transform(View);
        const auto P=T.Offset+(Model.Nodes[Selected].Position+FVector2D(80,54))*T.Scale;
        if(P.X<80||P.X>View.X-80||P.Y<60||P.Y>View.Y-60)Pan+=View*.5-P;
        ClampPan(View);Invalidate(EInvalidateWidgetReason::Layout);
    }
    TSlotlessChildren<SWidget> NodeChildren;
    FAetherInspectContext DragContext;TOptional<FAetherSkillNodeIdentity> DragNode;
    FAetherSkillTreeModel Model;FGraphSelection OnSelected;int32 Selected=INDEX_NONE;
    FVector2D Pan=FVector2D::ZeroVector;float Zoom=1;bool bPanning=false;
};
TSharedRef<SWidget> UAetherSkillGraphWidget::RebuildWidget()
{
    // UWidget 同步属性会覆盖 Slate Construct 的裁切设置，必须在拥有者上声明边界。
    SetClipping(EWidgetClipping::ClipToBoundsAlways);
    Graph=SNew(SAetherSkillGraph).OnSelected(FGraphSelection::CreateWeakLambda(this,[this](const auto& N,bool Detail){OnNodeSelected.Broadcast(N,Detail);}));
    Graph->SetModel(Model);RefreshNodes();return Graph.ToSharedRef();
}
void UAetherSkillGraphWidget::SetModel(const FAetherSkillTreeModel& InModel){Model=InModel;if(Graph){Graph->SetModel(Model);RefreshNodes();}}
void UAetherSkillGraphWidget::RefreshNodes()
{
    auto* Owner=GetTypedOuter<UUserWidget>();if(!Owner||!Graph)return;
    while(Nodes.Num()<Model.Nodes.Num())Nodes.Add(CreateWidget<UAetherSkillNodeWidget>(Owner,AetherWidgetAssets::Class<UAetherSkillNodeWidget>()));
    if(Nodes.Num()>Model.Nodes.Num())Nodes.SetNum(Model.Nodes.Num());
    TArray<TSharedRef<SWidget>> Widgets;
    for(int32 I=0;I<Nodes.Num();++I)
    {
        if(Nodes[I]){Nodes[I]->SetNode(Model.Nodes[I]);Widgets.Add(Nodes[I]->TakeWidget());}
        else {Widgets.Add(SNullWidget::NullWidget);UE_LOG(LogTemp,Error,TEXT("AETHER_SKILL_NODE_CREATE_FAILED %d"),I);}
    }
    Graph->SetNodes(Widgets);
}

void UAetherSkillGraphWidget::SelectNode(const FAetherSkillNodeIdentity& Node){if(Graph)Graph->SelectNode(Node);}
void UAetherSkillGraphWidget::ResetView(){if(Graph)Graph->ResetView();}
void UAetherSkillGraphWidget::CancelInteraction()
{
    if(!Graph)return;Graph->CancelInteraction();
    if(Graph->HasMouseCapture()&&FSlateApplication::IsInitialized())FSlateApplication::Get().ReleaseAllPointerCapture();
}
void UAetherSkillGraphWidget::ReleaseSlateResources(bool Children){CancelInteraction();Super::ReleaseSlateResources(Children);Graph.Reset();for(const auto& Node:Nodes)if(Node)Node->ReleaseSlateResources(Children);Nodes.Reset();}
