#include "Startup/AetherStartupPresentationSubsystem.h"
#include "Startup/AetherStartupOverlay.h"
#include "Startup/AetherStartupClient.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "RHI.h"
#include "Subsystems/SubsystemCollection.h"

namespace
{
bool NeedsOverlay(EAetherStartupStage Stage)
{return Stage!=EAetherStartupStage::Idle&&Stage!=EAetherStartupStage::Ready;}
}
bool UAetherStartupPresentationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const auto* GI=Cast<UGameInstance>(Outer);
    return Super::ShouldCreateSubsystem(Outer)&&GI&&!GI->IsDedicatedServerInstance()&&
        !IsRunningDedicatedServer()&&FApp::CanEverRender()&&!GUsingNullRHI;
}
void UAetherStartupPresentationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);Collection.InitializeDependency<UAetherStartupClient>();
    Client=GetGameInstance()->GetSubsystem<UAetherStartupClient>();
    if(Client.IsValid())Client->OnChanged.AddUObject(this,&UAetherStartupPresentationSubsystem::ViewChanged);
    ViewChanged();
}
void UAetherStartupPresentationSubsystem::ViewChanged()
{
    // RPC/Travel 回调只标脏；不在其广播栈里创建或拆除 Slate 树。
    bViewDirty=true;
}
bool UAetherStartupPresentationSubsystem::IsTickable() const
{return !IsTemplate()&&Client.IsValid()&&(bViewDirty||Overlay||NeedsOverlay(Client->GetView().Stage));}
TStatId UAetherStartupPresentationSubsystem::GetStatId() const
{RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherStartupPresentationSubsystem,STATGROUP_Tickables);}
UWorld* UAetherStartupPresentationSubsystem::GetTickableGameObjectWorld() const{return GetWorld();}
void UAetherStartupPresentationSubsystem::Tick(float){RefreshPresentation();}
void UAetherStartupPresentationSubsystem::RefreshPresentation()
{
    if(!Client.IsValid()||!NeedsOverlay(Client->GetView().Stage))
    {ReleasePresentation();bViewDirty=false;return;}
    auto* GI=GetGameInstance();auto* World=GI?GI->GetWorld():nullptr;
    auto* Viewport=GI?GI->GetGameViewportClient():nullptr;
    auto* Player=GI?GI->GetFirstGamePlayer():nullptr;
    if(!World||!World->IsGameWorld()||!Viewport||!Player||Player->GetGameInstance()!=GI||Viewport->GetGameInstance()!=GI)
    {ReleasePresentation();return;}
    auto* Controller=Player->GetPlayerController(World);
    if(Overlay&&(BoundWorld.Get()!=World||BoundViewport.Get()!=Viewport||BoundPlayer.Get()!=Player||
        BoundController.Get()!=Controller||BoundController.IsStale()))ReleasePresentation();
    if(!Overlay)
    {
        // GI 重载地图时 Widget 的 LocalPlayer 上下文显式跟随当前 World。
        // 先有 LocalPlayer 即可显示；不把无 PC 的 Loading 冒充服务端阶段。
        Overlay=CreateWidget<UAetherStartupOverlay>(GI,UAetherStartupOverlay::StaticClass());
        if(!Overlay)return;
        Overlay->SetPlayerContext(FLocalPlayerContext(Player,World));
        BoundWorld=World;BoundViewport=Viewport;BoundPlayer=Player;BoundController=Controller;
        Overlay->Present(Client.Get(),Client->GetView());
        Overlay->AddToViewport(100);Overlay->ActivateWidget();bViewDirty=false;
    }
    else
    {
        if(!Overlay->IsInViewport())
        {ReleasePresentation();return;} // 地图主动清空视口后，下一 Tick 创建新激活根。
        if(bViewDirty){Overlay->Present(Client.Get(),Client->GetView());bViewDirty=false;}
    }
    // PC 首次产生/替换时上面的身份分支重建激活根，让 CommonUI 对当前连接应用焦点与配置。
    // 没有 SetInputMode、bShowMouseCursor 或全局输入拦截的第二写者。
}
void UAetherStartupPresentationSubsystem::ReleasePresentation()
{
    if(Overlay){Overlay->ClearPresentation();Overlay->DeactivateWidget();Overlay->RemoveFromParent();Overlay=nullptr;}
    BoundWorld.Reset();BoundViewport.Reset();BoundPlayer.Reset();BoundController.Reset();bViewDirty=true;
}
void UAetherStartupPresentationSubsystem::Deinitialize()
{
    if(Client.IsValid())Client->OnChanged.RemoveAll(this);
    Client.Reset();ReleasePresentation();Super::Deinitialize();
}
