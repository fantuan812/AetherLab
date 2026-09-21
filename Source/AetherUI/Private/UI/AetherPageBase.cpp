#include "UI/AetherPageBase.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Networking/AetherCommandClient.h"
#include "Engine/LocalPlayer.h"
#include "TimerManager.h"
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
void UAetherPageBase::ProfileChanged(){if(IsOpen())RefreshPage();}
void UAetherPageBase::TickVisible(){if(IsOpen())LiveRefresh();}
void UAetherPageBase::NativeDestruct()
{
    if(bOpen)PageClosed();bOpen=false;if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(Timer);
    if(Menu.IsValid())Menu->OnChanged.RemoveAll(this);if(Client.IsValid())Client->OnChanged.RemoveAll(this);
    Menu.Reset();Client.Reset();Super::NativeDestruct();
}
