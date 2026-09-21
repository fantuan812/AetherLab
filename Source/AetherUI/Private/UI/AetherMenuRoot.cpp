#include "UI/AetherWidgetAssets.h"
#include "UI/AetherMenuRoot.h"
#include "Settings/AetherSettingsPage.h"
#include "UI/AetherFrontierHUD.h"
#include "AetherFrontierPanel.h"
#include "Dialogue/AetherDialoguePage.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "CommonInputBaseTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
namespace
{
FUIInputConfig MenuInput()
{
    FUIInputConfig C(ECommonInputMode::Menu,EMouseCaptureMode::NoCapture,EMouseLockMode::DoNotLock,false);
    C.bIgnoreMoveInput=true;C.bIgnoreLookInput=true;return C;
}
}
UAetherGameInputLayer::UAetherGameInputLayer(){bAutoActivate=true;bSupportsActivationFocus=false;}
TOptional<FUIInputConfig> UAetherGameInputLayer::GetDesiredInputConfig() const
{return FUIInputConfig(ECommonInputMode::Game,EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);}
UAetherModalLayer::UAetherModalLayer(){bIsModal=true;bIsBackHandler=true;SetIsFocusable(true);}
TSharedRef<SWidget> UAetherModalLayer::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(!WidgetTree->RootWidget)
    {
        Shield=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ModalShield"));
        Shield->SetBrushColor(FLinearColor(0,0,0,.8));Shield->SetPadding(FMargin(32));
        Shield->SetHorizontalAlignment(HAlign_Center);Shield->SetVerticalAlignment(VAlign_Center);WidgetTree->RootWidget=Shield;
    }
    if(BodyWidget)Shield->SetContent(BodyWidget);
    return Super::RebuildWidget();
}
void UAetherModalLayer::SetBody(UUserWidget* Body){BodyWidget=Body;if(Shield)Shield->SetContent(Body);}
UWidget* UAetherModalLayer::NativeGetDesiredFocusTarget() const{return BodyWidget?BodyWidget.Get():const_cast<UAetherModalLayer*>(this);}
TOptional<FUIInputConfig> UAetherModalLayer::GetDesiredInputConfig() const{return MenuInput();}
bool UAetherModalLayer::NativeOnHandleBackAction()
{
    if(auto* LP=GetOwningLocalPlayer())if(auto* M=LP->GetSubsystem<UAetherMenuSubsystem>())M->DismissLayer(Token);
    return true;
}
FReply UAetherModalLayer::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Escape||E.GetKey()==EKeys::Gamepad_FaceButton_Right){if(!E.IsRepeat())NativeOnHandleBackAction();return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(G,E);
}
UAetherMenuRoot* UAetherMenuRoot::Find(const UUserWidget& Context)
{
    const auto* PC=Context.GetOwningPlayer();const auto* HUD=PC?Cast<AAetherFrontierHUD>(PC->GetHUD()):nullptr;
    return HUD?HUD->MenuRoot.Get():nullptr;
}
TSharedRef<SWidget> UAetherMenuRoot::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(!WidgetTree->RootWidget)
    {
        auto* Overlay=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("MenuLayers"));WidgetTree->RootWidget=Overlay;
        GameLayer=CreateWidget<UAetherGameInputLayer>(GetOwningPlayer(),AetherWidgetAssets::Class<UAetherGameInputLayer>());Overlay->AddChildToOverlay(GameLayer);
        MainStack=WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(),TEXT("MainStack"));
        auto* Slot=Overlay->AddChildToOverlay(MainStack);Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);Slot->SetPadding(FMargin(32));
        ModalStack=WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(),TEXT("ModalStack"));
        auto* ModalSlot=Overlay->AddChildToOverlay(ModalStack);ModalSlot->SetHorizontalAlignment(HAlign_Fill);ModalSlot->SetVerticalAlignment(VAlign_Fill);
        MainStack->SetTransitionDuration(0);ModalStack->SetTransitionDuration(0);ModalStack->SetVisibility(ESlateVisibility::Collapsed);
        Panel=CreateWidget<UAetherFrontierPanel>(GetOwningPlayer(),AetherWidgetAssets::Class<UAetherFrontierPanel>());Dialogue=CreateWidget<UAetherDialoguePage>(GetOwningPlayer(),AetherWidgetAssets::Class<UAetherDialoguePage>());
    }
    return Super::RebuildWidget();
}
void UAetherMenuRoot::NativeConstruct()
{
    Super::NativeConstruct();SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(auto* LP=GetOwningLocalPlayer()){Menu=LP->GetSubsystem<UAetherMenuSubsystem>();Menu->OnChanged.AddUObject(this,&UAetherMenuRoot::Refresh);}
    UAetherSettingsPage::ApplyLocalPreferences(GetWorld());GameLayer->ActivateWidget();Refresh();
}
void UAetherMenuRoot::Refresh()
{
    if(!Menu.IsValid()||!MainStack||!ModalStack)return;
    TArray<FGuid> Remove;for(const auto& Pair:Modals)if(!Menu->HasLayer(Pair.Key))Remove.Add(Pair.Key);
    for(const FGuid Token:Remove)PopModal(Token);
    if(!Menu->IsOpen())
    {MainStack->ClearWidgets();MainStack->SetVisibility(ESlateVisibility::Collapsed);GameLayer->ActivateWidget();return;}
    MainStack->SetVisibility(ESlateVisibility::Visible);
    UCommonActivatableWidget* Desired=Menu->GetPage()==EAetherMenuPage::Dialogue?static_cast<UCommonActivatableWidget*>(Dialogue):static_cast<UCommonActivatableWidget*>(Panel);
    if(MainStack->GetActiveWidget()!=Desired)
    {MainStack->ClearWidgets();MainStack->AddWidgetInstance(*Desired);}
    Desired->ActivateWidget();
}
bool UAetherMenuRoot::PushModal(FGuid Token,UUserWidget* Body)
{
    if(!Token.IsValid()||!Body||!Menu.IsValid()||!Menu->HasLayer(Token)||Modals.Contains(Token))return false;
    auto* Layer=CreateWidget<UAetherModalLayer>(GetOwningPlayer(),AetherWidgetAssets::Class<UAetherModalLayer>());Layer->Token=Token;Layer->SetBody(Body);
    Modals.Add(Token,Layer);ModalStack->SetVisibility(ESlateVisibility::Visible);ModalStack->AddWidgetInstance(*Layer);return true;
}
void UAetherMenuRoot::PopModal(FGuid Token)
{
    auto* Entry=Modals.Find(Token);if(!Entry)return;auto* Layer=Entry->Get();Modals.Remove(Token);
    if(Layer){Layer->SetBody(nullptr);ModalStack->RemoveWidget(*Layer);}
    if(Modals.IsEmpty())ModalStack->SetVisibility(ESlateVisibility::Collapsed);
}
void UAetherMenuRoot::NativeDestruct()
{
    if(Menu.IsValid())Menu->OnChanged.RemoveAll(this);Menu.Reset();
    if(ModalStack)ModalStack->ClearWidgets();Modals.Reset();if(MainStack)MainStack->ClearWidgets();
    if(GameLayer)GameLayer->DeactivateWidget();Super::NativeDestruct();
}
