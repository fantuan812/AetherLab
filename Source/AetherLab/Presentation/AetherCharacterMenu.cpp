#include "Characters/AetherFrontierCharacter.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

UAetherMenuSubsystem* AAetherFrontierCharacter::MenuSubsystem() const
{
    const auto* PC=Cast<APlayerController>(Controller);
    // 复制顺序可能暂时留下旧 Controller 引用；只有控制器当前 Pawn 才能更改本地菜单。
    return PC&&PC->GetPawn()==this&&PC->GetLocalPlayer()?PC->GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>():nullptr;
}
void AAetherFrontierCharacter::SelectPanel(int32 NewPanel)
{
    if(NewPanel<1||NewPanel>6)return;
    if(auto* Menu=MenuSubsystem()){Menu->AttachPawn(this);Menu->TogglePage(EAetherMenuPage(NewPanel));return;}
    // 无 LocalPlayer 的专服测试与机器人仅更新兼容镜像，不创建任何界面或输入依赖。
    if(bPanel&&Panel==NewPanel){ClosePanel();return;}OpenPanel(NewPanel);
}
void AAetherFrontierCharacter::OpenPanel(int32 NewPanel)
{
    if(NewPanel<1||NewPanel>6)return;
    if(auto* Menu=MenuSubsystem()){Menu->AttachPawn(this);Menu->OpenPage(EAetherMenuPage(NewPanel));return;}
    if(NewPanel!=1)CloseTrade();Panel=NewPanel;bPanel=true;ReleaseHeldInput();OnPresentationChanged.Broadcast();
}
void AAetherFrontierCharacter::ClosePanel()
{
    if(auto* Menu=MenuSubsystem()){Menu->Close();return;}
    CloseTrade();bPanel=false;Panel=0;OnPresentationChanged.Broadcast();
}
void AAetherFrontierCharacter::MenuBack()
{
    // 旧出售确认在正式模态资产接入前也必须先退回详情，不能一次 Escape 就确认/关闭交易。
    if(!SaleConfirmationText().IsEmpty()){SaleConfirmation={};OnPresentationChanged.Broadcast();return;}
    if(auto* Menu=MenuSubsystem()){Menu->Back();return;}
    if(bPanel)ClosePanel();else OpenPanel(6);
}
