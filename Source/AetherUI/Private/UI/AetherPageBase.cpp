#include "UI/AetherPageBase.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Networking/AetherCommandClient.h"
#include "Engine/LocalPlayer.h"
#include "TimerManager.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "Components/CheckBox.h"
#include "Components/InputKeySelector.h"
#include "Components/ScrollBox.h"
#include "InputCoreTypes.h"
TArray<UWidget*> UAetherPageBase::FocusableWidgets() const
{
    TArray<UWidget*> Out,All;if(!WidgetTree)return Out;WidgetTree->GetAllWidgets(All);
    for(auto* W:All)
        if(W&&W!=this&&W->IsVisible()&&W->GetIsEnabled()&&W->SupportsKeyboardFocus()&&
           (W->IsA<UButton>()||W->IsA<UEditableTextBox>()||W->IsA<UComboBoxString>()||W->IsA<USpinBox>()||W->IsA<UCheckBox>()||W->IsA<UInputKeySelector>()||W->IsA<UUserWidget>()))Out.Add(W);
    return Out;
}
void UAetherPageBase::RestoreFocusIndex(int32 Index)
{
    if(!IsOpen())return;const auto Widgets=FocusableWidgets();if(Widgets.IsValidIndex(Index))Widgets[Index]->SetUserFocus(GetOwningPlayer());
}
FReply UAetherPageBase::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    const FKey Key=E.GetKey();if(Key!=EKeys::Tab&&Key!=EKeys::Gamepad_LeftTrigger&&Key!=EKeys::Gamepad_RightTrigger)return Super::NativeOnPreviewKeyDown(G,E);
    if(!IsOpen()||!Menu.IsValid()||Menu->GetLayerCount()>0)return Super::NativeOnPreviewKeyDown(G,E);
    const auto Widgets=FocusableWidgets();if(Widgets.IsEmpty())return Super::NativeOnPreviewKeyDown(G,E);
    for(auto* W:Widgets)if(const auto* Selector=Cast<UInputKeySelector>(W);Selector&&Selector->GetIsSelectingKey())return Super::NativeOnPreviewKeyDown(G,E);
    int32 Current=INDEX_NONE;for(int32 I=0;I<Widgets.Num();++I)if(Widgets[I]->HasUserFocus(GetOwningPlayer())||Widgets[I]->HasUserFocusedDescendants(GetOwningPlayer())){Current=I;break;}
    const int32 Direction=E.IsShiftDown()||Key==EKeys::Gamepad_LeftTrigger?-1:1;
    const int32 Next=(Current+Direction+Widgets.Num())%Widgets.Num();auto* Target=Widgets[Next];Target->SetUserFocus(GetOwningPlayer());
    for(auto* Parent=Target->GetParent();Parent;Parent=Parent->GetParent())if(auto* Scroll=Cast<UScrollBox>(Parent)){Scroll->ScrollWidgetIntoView(Target,true);break;}
    return FReply::Handled();
}
void UAetherPageBase::NativeConstruct()
{
    Super::NativeConstruct();SetIsFocusable(true);
    if(auto* LP=GetOwningLocalPlayer())
    {
        Menu=LP->GetSubsystem<UAetherMenuSubsystem>();Client=LP->GetSubsystem<UAetherCommandClient>();
        Menu->OnChanged.AddUObject(this,&UAetherPageBase::MenuChanged);
        Client->OnChanged.AddUObject(this,&UAetherPageBase::ProfileChanged);
    }
    MenuChanged();
}
bool UAetherPageBase::IsOpen() const{return Menu.IsValid()&&Menu->GetPage()==Page;}
AAetherFrontierCharacter* UAetherPageBase::Player() const{return Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());}
const FAetherProfileStateV10* UAetherPageBase::Profile() const{return Client.IsValid()&&Client->GetProfile().IsSet()?&Client->GetProfile().GetValue():nullptr;}
void UAetherPageBase::MenuChanged()
{
    const bool Now=IsOpen();
    if(Now&&!bOpen){bOpen=true;RefreshPage();GetWorld()->GetTimerManager().SetTimer(Timer,this,&UAetherPageBase::TickVisible,.25f,true);}
    else if(!Now&&bOpen){bOpen=false;GetWorld()->GetTimerManager().ClearTimer(Timer);PageClosed();}
}
void UAetherPageBase::ProfileChanged()
{
    if(!IsOpen())return;
    int32 FocusIndex=INDEX_NONE;const auto Previous=FocusableWidgets();for(int32 I=0;I<Previous.Num();++I)
        if(Previous[I]->HasUserFocus(GetOwningPlayer())||Previous[I]->HasUserFocusedDescendants(GetOwningPlayer())){FocusIndex=I;break;}
    RefreshPage();
    if(FocusIndex!=INDEX_NONE&&Menu.IsValid()&&Menu->GetLayerCount()==0&&GetWorld())
        GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this,FocusIndex]{if(Menu.IsValid()&&Menu->GetLayerCount()==0)RestoreFocusIndex(FocusIndex);}));
}
void UAetherPageBase::TickVisible(){if(IsOpen())LiveRefresh();}
void UAetherPageBase::NativeDestruct()
{
    if(bOpen)PageClosed();bOpen=false;if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(Timer);
    if(Menu.IsValid())Menu->OnChanged.RemoveAll(this);if(Client.IsValid())Client->OnChanged.RemoveAll(this);
    Menu.Reset();Client.Reset();Super::NativeDestruct();
}
