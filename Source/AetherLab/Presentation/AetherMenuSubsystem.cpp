#include "Presentation/AetherMenuSubsystem.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

bool UAetherMenuSubsystem::ValidPage(EAetherMenuPage Page)
{return Page>=EAetherMenuPage::Inventory&&Page<=EAetherMenuPage::System;}
void UAetherMenuSubsystem::TogglePage(EAetherMenuPage Page)
{if(ValidPage(Page))SetPage(CurrentPage==Page?EAetherMenuPage::None:Page);}
void UAetherMenuSubsystem::OpenPage(EAetherMenuPage Page)
{if(ValidPage(Page))SetPage(Page);}
void UAetherMenuSubsystem::SetPage(EAetherMenuPage Page)
{
    if(Page==CurrentPage)return;
    const bool WasOpen=IsOpen();CurrentPage=Page;Layers.Reset();
    Publish(WasOpen);
}
void UAetherMenuSubsystem::Back()
{
    if(!Layers.IsEmpty()){Layers.Pop();OnChanged.Broadcast();return;}
    SetPage(IsOpen()?EAetherMenuPage::None:EAetherMenuPage::System);
}
void UAetherMenuSubsystem::Close(){SetPage(EAetherMenuPage::None);}
FGuid UAetherMenuSubsystem::PushLayer(FName Layer)
{
    if(!IsOpen()||Layer.IsNone()||Layers.Num()>=8)return {};
    const FGuid Token=FGuid::NewGuid();Layers.Add({Token,Layer});OnChanged.Broadcast();return Token;
}
bool UAetherMenuSubsystem::DismissLayer(FGuid Token)
{
    if(!Token.IsValid()||Layers.IsEmpty()||Layers.Last().Token!=Token)return false;
    Layers.Pop();OnChanged.Broadcast();return true;
}
void UAetherMenuSubsystem::SavePageMemory(EAetherMenuPage Page,const FAetherMenuPageMemory& Memory)
{
    if(!ValidPage(Page))return;
    auto& Saved=PageMemory.FindOrAdd(Page);Saved=Memory;
    Saved.Search=Saved.Search.Left(256);
    Saved.ScrollOffset=FMath::IsFinite(Saved.ScrollOffset)?FMath::Max(0.f,Saved.ScrollOffset):0.f;
}
FAetherMenuPageMemory UAetherMenuSubsystem::GetPageMemory(EAetherMenuPage Page) const
{return PageMemory.FindRef(Page);}
void UAetherMenuSubsystem::Publish(bool WasOpen)
{
    if(auto* C=BoundPawn.Get())
    {
        // bPanel/Panel 暂留作旧玩法的只读镜像；所有正式菜单入口都走本子系统。
        C->bPanel=IsOpen();C->Panel=int32(CurrentPage);
        if(!IsOpen()||CurrentPage!=EAetherMenuPage::Inventory)C->CloseTrade();
        if(IsOpen()&&!WasOpen)C->ReleaseHeldInput();
    }
    if(IsOpen()!=WasOpen)
    {
        if(auto* PC=GetLocalPlayer()->GetPlayerController(GetWorld());PC&&PC->IsLocalController())
        {
            // 过渡期唯一输入配置入口。页面切换不重复 Flush，也不在 Widget Tick 中抢焦点。
            PC->FlushPressedKeys();PC->bShowMouseCursor=IsOpen();
            if(IsOpen())
            {
                FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);
                Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);
            }
            else PC->SetInputMode(FInputModeGameOnly());
        }
    }
    OnChanged.Broadcast();
}
void UAetherMenuSubsystem::AttachPawn(AAetherFrontierCharacter* Pawn)
{
    if((Pawn&&BoundPawn.Get()==Pawn)||(!Pawn&&BoundPawn.IsExplicitlyNull()&&!IsOpen()&&PageMemory.IsEmpty()))return;
    const bool WasOpen=IsOpen();
    if(auto* Old=BoundPawn.Get()){Old->ReleaseHeldInput();Old->CloseTrade();Old->bPanel=false;Old->Panel=0;}
    BoundPawn=Pawn;CurrentPage=EAetherMenuPage::None;Layers.Reset();PageMemory.Reset();
    // 角色换代使弹窗、搜索与实例选择同时失效，防止对旧角色发送按钮命令。
    Publish(WasOpen);
}
void UAetherMenuSubsystem::Deinitialize()
{AttachPawn(nullptr);OnChanged.Clear();Super::Deinitialize();}
