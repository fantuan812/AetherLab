#pragma once
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "AetherPageBase.generated.h"
class AAetherFrontierCharacter;
class UAetherCommandClient;
struct FAetherProfileStateV10;
UCLASS()
class AETHERUI_API UAetherPageButton : public UButton
{
    GENERATED_BODY()
public:
    FSimpleDelegate Action;
    void Bind(FSimpleDelegate In){Action=MoveTemp(In);OnClicked.AddUniqueDynamic(this,&UAetherPageButton::Invoke);}
private:
    UFUNCTION() void Invoke(){Action.ExecuteIfBound();}
};
/** 页面只在打开时订阅后的事件/低频实时刷新工作；命令始终由 LocalPlayer 服务发送。 */
UCLASS(Abstract)
class AETHERUI_API UAetherPageBase : public UUserWidget
{
    GENERATED_BODY()
public:
    EAetherMenuPage Page=EAetherMenuPage::None;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual UWidget* InitialFocus() const{return const_cast<UAetherPageBase*>(this);}
    virtual void RefreshPage(){}
    virtual void LiveRefresh(){}
    virtual void PageClosed(){}
    AAetherFrontierCharacter* Player() const;
    const FAetherProfileStateV10* Profile() const;
    bool IsOpen() const;
protected:
    TWeakObjectPtr<UAetherMenuSubsystem> Menu;
    TWeakObjectPtr<UAetherCommandClient> Client;
private:
    bool bOpen=false;
    void MenuChanged();
    void ProfileChanged();
    void TickVisible();
    FTimerHandle Timer;
};
