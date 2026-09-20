#include "Preview/AetherCharacterPreviewWidget.h"
#include "Preview/AetherCharacterPreviewSubsystem.h"
#include "AetherCombat.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/Border.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Framework/Application/SlateApplication.h"

TSharedRef<SWidget> UAetherCharacterPreviewWidget::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
    if(!WidgetTree->RootWidget)
    {
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();WidgetTree->RootWidget=Root;
        auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor(.025f,.035f,.05f,1));
        Root->AddChildToVerticalBox(Frame)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Scale=WidgetTree->ConstructWidget<UScaleBox>();Scale->SetStretch(EStretch::ScaleToFit);Frame->SetContent(Scale);
        Image=WidgetTree->ConstructWidget<UImage>();Scale->SetContent(Image);
        auto* Controls=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Controls);
        auto Add=[&](const TCHAR* Label)
        {
            auto* B=WidgetTree->ConstructWidget<UButton>();auto* T=WidgetTree->ConstructWidget<UTextBlock>();
            T->SetText(FText::FromString(Label));B->SetContent(T);
            Controls->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            return B;
        };
        auto* Left=Add(TEXT("左转"));Left->OnClicked.AddDynamic(this,&UAetherCharacterPreviewWidget::TurnLeft);
        auto* Right=Add(TEXT("右转"));Right->OnClicked.AddDynamic(this,&UAetherCharacterPreviewWidget::TurnRight);
        auto* In=Add(TEXT("+"));In->OnClicked.AddDynamic(this,&UAetherCharacterPreviewWidget::ZoomIn);
        auto* Out=Add(TEXT("−"));Out->OnClicked.AddDynamic(this,&UAetherCharacterPreviewWidget::ZoomOut);
        auto* Reset=Add(TEXT("重置"));Reset->OnClicked.AddDynamic(this,&UAetherCharacterPreviewWidget::ResetCamera);
        Caption=WidgetTree->ConstructWidget<UTextBlock>();Caption->SetAutoWrapText(true);
        Root->AddChildToVerticalBox(Caption)->SetPadding(FMargin(0,8));
    }
    return Super::RebuildWidget();
}
void UAetherCharacterPreviewWidget::NativeConstruct()
{
    Super::NativeConstruct();SetIsFocusable(true);
    if(auto* LP=GetOwningLocalPlayer())
    {
        Preview=LP->GetSubsystem<UAetherCharacterPreviewSubsystem>();
        if(Preview.IsValid())
        {
            Preview->OnChanged.RemoveAll(this);
            Preview->OnChanged.AddUObject(this,&UAetherCharacterPreviewWidget::RefreshDisplay);
            if(RequestedSource.IsValid())Preview->Acquire(this,RequestedSource.Get());
        }
    }
    RefreshDisplay();
}
void UAetherCharacterPreviewWidget::NativeDestruct()
{
    bDragging=false;
    if(Preview.IsValid()){Preview->OnChanged.RemoveAll(this);Preview->Release(this);}
    Preview.Reset();RequestedSource.Reset();Super::NativeDestruct();
}
void UAetherCharacterPreviewWidget::SetSource(AAetherCharacter* Character)
{
    RequestedSource=Character;
    if(!Character)
    {
        bDragging=false;
        // 拖动时按 Esc 切页也要释放捕获，不能等下一次鼠标抬起阻塞其他页面。
        const auto Cached=GetCachedWidget();
        if(Cached.IsValid()&&Cached->HasMouseCapture()&&FSlateApplication::IsInitialized())
            FSlateApplication::Get().ReleaseAllPointerCapture();
    }
    if(Preview.IsValid()){if(Character)Preview->Acquire(this,Character);else Preview->Release(this);}
}
void UAetherCharacterPreviewWidget::RefreshDisplay()
{
    if(!Image||!Caption)return;
    FSlateBrush Brush;Brush.ImageSize=FVector2D(768,1024);
    if(Preview.IsValid())
    {
        UObject* Resource=Preview->GetDisplayMaterial()?static_cast<UObject*>(Preview->GetDisplayMaterial()):
            static_cast<UObject*>(Preview->GetRenderTarget());
        Brush.SetResourceObject(Resource);Caption->SetText(Preview->GetStatus());
    }
    else Caption->SetText(FText::GetEmpty());
    Image->SetBrush(Brush);
}
FReply UAetherCharacterPreviewWidget::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()==EKeys::LeftMouseButton&&Preview.IsValid()&&Preview->IsPreviewActive())
    {bDragging=true;return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget());}
    return Super::NativeOnMouseButtonDown(G,E);
}
FReply UAetherCharacterPreviewWidget::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()==EKeys::LeftMouseButton&&bDragging){bDragging=false;return FReply::Handled().ReleaseMouseCapture();}
    return Super::NativeOnMouseButtonUp(G,E);
}
FReply UAetherCharacterPreviewWidget::NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E)
{
    if(bDragging&&Preview.IsValid())
    {
        // 局部坐标差自动吸收 DPI 缩放，拖动只改变预览轨道相机，不发真实角色转向 RPC。
        const FVector2D Delta=G.AbsoluteToLocal(E.GetScreenSpacePosition())-G.AbsoluteToLocal(E.GetLastScreenSpacePosition());
        Preview->Rotate(-float(Delta.X)*.4f,float(Delta.Y)*.25f);return FReply::Handled();
    }
    return Super::NativeOnMouseMove(G,E);
}
FReply UAetherCharacterPreviewWidget::NativeOnMouseWheel(const FGeometry& G,const FPointerEvent& E)
{if(Preview.IsValid()){Preview->Zoom(-E.GetWheelDelta()*.1f);return FReply::Handled();}return Super::NativeOnMouseWheel(G,E);}
void UAetherCharacterPreviewWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& E)
{bDragging=false;Super::NativeOnMouseCaptureLost(E);}
FReply UAetherCharacterPreviewWidget::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Gamepad_FaceButton_Top||E.GetKey()==EKeys::Home)
    {if(!E.IsRepeat())ResetCamera();return FReply::Handled();}
    return Super::NativeOnKeyDown(G,E);
}
FReply UAetherCharacterPreviewWidget::NativeOnAnalogValueChanged(const FGeometry& G,const FAnalogInputEvent& E)
{
    if(Preview.IsValid()&&Preview->IsPreviewActive())
    {
        const float V=E.GetAnalogValue(),Dt=FMath::Clamp(GetWorld()->GetDeltaSeconds(),0.f,.05f);
        if(E.GetKey()==EKeys::Gamepad_RightX){if(FMath::Abs(V)>.15f)Preview->Rotate(-V*120.f*Dt,0);return FReply::Handled();}
        if(E.GetKey()==EKeys::Gamepad_RightY){if(FMath::Abs(V)>.15f)Preview->Rotate(0,V*60.f*Dt);return FReply::Handled();}
        if(E.GetKey()==EKeys::Gamepad_LeftTriggerAxis){Preview->Zoom(V*.6f*Dt);return FReply::Handled();}
        if(E.GetKey()==EKeys::Gamepad_RightTriggerAxis){Preview->Zoom(-V*.6f*Dt);return FReply::Handled();}
    }
    return Super::NativeOnAnalogValueChanged(G,E);
}
void UAetherCharacterPreviewWidget::ResetCamera(){if(Preview.IsValid())Preview->ResetView();}
void UAetherCharacterPreviewWidget::TurnLeft(){if(Preview.IsValid())Preview->Rotate(-30,0);}
void UAetherCharacterPreviewWidget::TurnRight(){if(Preview.IsValid())Preview->Rotate(30,0);}
void UAetherCharacterPreviewWidget::ZoomIn(){if(Preview.IsValid())Preview->Zoom(-.1f);}
void UAetherCharacterPreviewWidget::ZoomOut(){if(Preview.IsValid())Preview->Zoom(.1f);}
