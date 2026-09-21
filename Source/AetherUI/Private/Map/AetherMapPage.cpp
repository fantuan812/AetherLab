#include "Map/AetherMapPage.h"
#include "UI/AetherWidgetAssets.h"
#include "UI/AetherPageWidgets.h"
#include "Definitions/AetherMapDefinition.h"
#include "Definitions/AetherWorldDefinition.h"
#include "Framework/AetherFrontier.h"
#include "Quests/AetherGuide.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "EngineUtils.h"
using namespace AetherPageWidgets;
TSharedRef<SWidget> UAetherMapCanvas::RebuildWidget()
{
    SetIsFocusable(true);SetClipping(EWidgetClipping::ClipToBounds);
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(WidgetTree->RootWidget)AetherWidgetAssets::BindDesigner(*this,*WidgetTree);
    if(!WidgetTree->RootWidget){auto* B=WidgetTree->ConstructWidget<UBorder>();B->SetBrushColor(FLinearColor(.025,.04,.055));WidgetTree->RootWidget=B;}
    return Super::RebuildWidget();
}
FVector2D UAetherMapCanvas::Point(const FGeometry& G,FVector2D World) const
{
    const float Scale=FMath::Min(G.GetLocalSize().X,G.GetLocalSize().Y)/82000.f*Zoom;
    return G.GetLocalSize()*.5f+FVector2D(World.X-Pan.X,-World.Y+Pan.Y)*Scale;
}
int32 UAetherMapCanvas::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const
{
    Layer=Super::NativePaint(Args,G,Clip,Out,Layer,Style,Enabled);const auto* Brush=FCoreStyle::Get().GetBrush("WhiteBrush");
    const auto Font=FCoreStyle::GetDefaultFontStyle("Regular",13);
    auto Label=[&](FVector2D P,const FString& S,FLinearColor C){FSlateDrawElement::MakeText(Out,Layer+3,G.ToPaintGeometry(FVector2D(260,30),FSlateLayoutTransform(P)),S,Font,ESlateDrawEffect::None,C);};
    for(const auto& R:FAetherMapDefinitions::Get().Regions)
    {
        const FVector2D A=Point(G,R.Center+FVector2D(-R.Extent.X,R.Extent.Y)),B=Point(G,R.Center+FVector2D(R.Extent.X,-R.Extent.Y));
        FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(B-A,FSlateLayoutTransform(A)),Brush,ESlateDrawEffect::None,R.Color);
        Label(Point(G,R.Center)+FVector2D(5,10),R.Label,FLinearColor(.7,.76,.8));
        if(R.Center.SizeSquared()>0)FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),{Point(G,FVector2D::ZeroVector),Point(G,R.Center)},ESlateDrawEffect::None,FLinearColor(.4,.37,.27),true,3.f);
    }
    for(const auto& M:Markers)
    {
        const FVector2D P=Point(G,FVector2D(M.Position.X,M.Position.Y));
        if(P.X<0||P.Y<0||P.X>G.GetLocalSize().X||P.Y>G.GetLocalSize().Y)continue;
        FSlateDrawElement::MakeBox(Out,Layer+2,G.ToPaintGeometry(FVector2D(9,9),FSlateLayoutTransform(P-FVector2D(4.5,4.5))),Brush,ESlateDrawEffect::None,M.Color);
        if(M.bPlayer)
        {
            const float A=FMath::DegreesToRadians(M.Yaw);const FVector2D D(FMath::Cos(A),-FMath::Sin(A)),Side(-D.Y,D.X);
            FSlateDrawElement::MakeLines(Out,Layer+3,G.ToPaintGeometry(),{P-D*6+Side*6,P+D*14,P-D*6-Side*6},ESlateDrawEffect::None,M.Color,true,2);
        }
    }
    Label(FVector2D(10,10),TEXT("北 ↑ · 滚轮缩放 / 拖动平移 / 手柄方向键平移、肩键选点"),FLinearColor::White);
    return Layer+4;
}
FReply UAetherMapCanvas::NativeOnMouseWheel(const FGeometry&,const FPointerEvent& E){ChangeZoom(FMath::Pow(1.15f,E.GetWheelDelta()));return FReply::Handled();}
FReply UAetherMapCanvas::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Handled();
    const FVector2D P=G.AbsoluteToLocal(E.GetScreenSpacePosition());double Best=225;int32 Index=INDEX_NONE;
    for(int32 I=0;I<Markers.Num();++I){const double D=(Point(G,FVector2D(Markers[I].Position.X,Markers[I].Position.Y))-P).SizeSquared();if(D<Best){Best=D;Index=I;}}
    if(Index!=INDEX_NONE){SelectedIndex=Index;Selected.ExecuteIfBound(Markers[Index]);return FReply::Handled().SetUserFocus(TakeWidget());}
    bDrag=true;return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget());
}
FReply UAetherMapCanvas::NativeOnMouseButtonUp(const FGeometry&,const FPointerEvent&){bDrag=false;return FReply::Handled().ReleaseMouseCapture();}
FReply UAetherMapCanvas::NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E)
{
    if(bDrag&&HasMouseCapture())
    {
        const float Scale=FMath::Max(.001f,FMath::Min(G.GetLocalSize().X,G.GetLocalSize().Y)/82000.f*Zoom);
        const FVector2D Delta=E.GetCursorDelta()/G.Scale;Pan+=FVector2D(-Delta.X,Delta.Y)/Scale;
        Pan.X=FMath::Clamp(Pan.X,-41000.,41000.);Pan.Y=FMath::Clamp(Pan.Y,-41000.,41000.);
        InvalidateLayoutAndVolatility();return FReply::Handled();
    }
    return Super::NativeOnMouseMove(G,E);
}
FReply UAetherMapCanvas::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    const FKey K=E.GetKey();bool Handled=true;
    if(K==EKeys::Gamepad_DPad_Up||K==EKeys::Up)Pan.Y+=1800/Zoom;
    else if(K==EKeys::Gamepad_DPad_Down||K==EKeys::Down)Pan.Y-=1800/Zoom;
    else if(K==EKeys::Gamepad_DPad_Left||K==EKeys::Left)Pan.X-=1800/Zoom;
    else if(K==EKeys::Gamepad_DPad_Right||K==EKeys::Right)Pan.X+=1800/Zoom;
    else if(K==EKeys::Gamepad_LeftTrigger)ChangeZoom(1/1.15f);
    else if(K==EKeys::Gamepad_RightTrigger)ChangeZoom(1.15f);
    else if((K==EKeys::Gamepad_FaceButton_Left||K==EKeys::Gamepad_FaceButton_Top)&&!Markers.IsEmpty())
    {SelectedIndex=(SelectedIndex+(K==EKeys::Gamepad_FaceButton_Top?1:Markers.Num()-1))%Markers.Num();Selected.ExecuteIfBound(Markers[SelectedIndex]);}
    else Handled=false;
    if(Handled){InvalidateLayoutAndVolatility();return FReply::Handled();}return Super::NativeOnKeyDown(G,E);
}
TSharedRef<SWidget> UAetherMapPage::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(WidgetTree->RootWidget)AetherWidgetAssets::BindDesigner(*this,*WidgetTree);
    if(!WidgetTree->RootWidget)
    {
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();WidgetTree->RootWidget=Root;
        auto* Toolbar=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Toolbar);
        auto Add=[&](const FString& Label,FSimpleDelegate Action){auto* Col=WidgetTree->ConstructWidget<UVerticalBox>();Toolbar->AddChildToHorizontalBox(Col);return Button(*WidgetTree,*Col,Label,MoveTemp(Action));};
        Add(TEXT("服务标记"),FSimpleDelegate::CreateWeakLambda(this,[this](){bServices=!bServices;RefreshPage();}));
        Add(TEXT("队友"),FSimpleDelegate::CreateWeakLambda(this,[this](){bParty=!bParty;RefreshPage();}));
        Add(TEXT("任务目标"),FSimpleDelegate::CreateWeakLambda(this,[this](){bQuests=!bQuests;RefreshPage();}));
        Add(TEXT("放大"),FSimpleDelegate::CreateWeakLambda(this,[this](){Map->ChangeZoom(1.2f);}));
        Add(TEXT("缩小"),FSimpleDelegate::CreateWeakLambda(this,[this](){Map->ChangeZoom(1/1.2f);}));
        Add(TEXT("居中"),FSimpleDelegate::CreateWeakLambda(this,[this](){Map->Pan=FVector2D::ZeroVector;Map->Zoom=1;}));
        Map=CreateWidget<UAetherMapCanvas>(GetOwningPlayer());Root->AddChildToVerticalBox(Map)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Map->Selected.BindUObject(this,&UAetherMapPage::Select);
        Detail=Text(*WidgetTree,*Root,TEXT("选择据点、任务或服务标记查看详情。"));
        TravelButton=Button(*WidgetTree,*Root,TEXT("传送到选中据点"),FSimpleDelegate::CreateUObject(this,&UAetherMapPage::Travel),false);
    }

    const auto Bind=[&](const TCHAR* Name,FSimpleDelegate Action){if(auto* B=Cast<UAetherPageButton>(GetWidgetFromName(Name)))B->Bind(MoveTemp(Action));};
    Bind(TEXT("ServicesButton"),FSimpleDelegate::CreateWeakLambda(this,[this](){bServices=!bServices;RefreshPage();}));
    Bind(TEXT("PartyButton"),FSimpleDelegate::CreateWeakLambda(this,[this](){bParty=!bParty;RefreshPage();}));
    Bind(TEXT("QuestsButton"),FSimpleDelegate::CreateWeakLambda(this,[this](){bQuests=!bQuests;RefreshPage();}));
    Bind(TEXT("ZoomInButton"),FSimpleDelegate::CreateWeakLambda(this,[this](){Map->ChangeZoom(1.2f);}));
    Bind(TEXT("ZoomOutButton"),FSimpleDelegate::CreateWeakLambda(this,[this](){Map->ChangeZoom(1/1.2f);}));
    Bind(TEXT("CenterButton"),FSimpleDelegate::CreateWeakLambda(this,[this](){Map->Pan=FVector2D::ZeroVector;Map->Zoom=1;}));
    Bind(TEXT("TravelButton"),FSimpleDelegate::CreateUObject(this,&UAetherMapPage::Travel));
    if(Map)Map->Selected.BindUObject(this,&UAetherMapPage::Select);
    return Super::RebuildWidget();
}
UWidget* UAetherMapPage::InitialFocus() const{return Map?Map.Get():Super::InitialFocus();}
void UAetherMapPage::RefreshPage(){LiveRefresh();}
void UAetherMapPage::LiveRefresh()
{
    auto* C=Player();const auto* P=Profile();if(!C||!P||!Map)return;Map->Markers.Reset();
    for(const auto& B:FAetherMapDefinitions::Get().Beacons)
    {
        const bool Unlocked=B.RequiredQuest.IsNone()||P->Claims.Contains(B.RequiredQuest.ToString());
        Map->Markers.Add({B.Id.ToString(),B.Label+(Unlocked?TEXT(" · 已解锁"):TEXT(" · 尚未解锁")),B.Position,Unlocked?FLinearColor(1,.8,.3):FLinearColor(.4,.4,.4),B.Id});
    }
    if(bServices)for(const auto& O:FAetherWorldDefinitions::Get().Objects)
        if(!O.Service.IsNone()&&(O.Section=="Town"||O.Service=="Teacher"||O.Service=="Inn"||O.Service=="Shop"||O.Service=="Register"))
            Map->Markers.Add({O.Id.ToString(),O.Label,O.Location,FLinearColor(.3,.7,1)});
    if(bQuests){const auto Guidance=AetherGuide::Resolve(C);if(Guidance.bHasTarget)Map->Markers.Add({TEXT("TrackedQuest"),Guidance.Label,Guidance.Position,FLinearColor(1,.5,.15)});}
    Map->Markers.Add({TEXT("Self"),TEXT("你"),C->GetActorLocation(),FLinearColor(.2,1,.5),NAME_None,true,float(C->GetActorRotation().Yaw)});
    if(bParty)for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(*It!=C&&It->Fighter==EAetherFighter::Player)
    {
        const auto* PS=It->ProfileState();const auto* Owner=It->CompanionOwner?It->CompanionOwner->ProfileState():nullptr;
        if(C->ProfileState()&&((PS&&PS->PartyLeader==C->ProfileState()->PartyLeader)||(Owner&&Owner->PartyLeader==C->ProfileState()->PartyLeader)))
            Map->Markers.Add({It->GetName(),PS?PS->DisplayName:It->CompanionId.ToString(),It->GetActorLocation(),FLinearColor(.3,.8,1),NAME_None,true,float(It->GetActorRotation().Yaw)});
    }
    Map->InvalidateLayoutAndVolatility();
}
void UAetherMapPage::Select(const FAetherMapMarker& M)
{
    SelectedBeacon=M.Beacon;Detail->SetText(FText::FromString(M.Label));const auto* P=Profile();
    const auto* B=FAetherMapDefinitions::Get().Beacons.FindByPredicate([&](const auto& V){return V.Id==SelectedBeacon;});
    TravelButton->SetIsEnabled(P&&B&&(B->RequiredQuest.IsNone()||P->Claims.Contains(B->RequiredQuest.ToString())));
}
void UAetherMapPage::Travel()
{
    if(auto* C=Player())if(const auto* P=Profile();P&&!SelectedBeacon.IsNone())
    {C->ServerMapTravel(SelectedBeacon,P->Revision);Detail->SetText(FText::FromString(TEXT("等待服务器确认安全传送。")));}
}
