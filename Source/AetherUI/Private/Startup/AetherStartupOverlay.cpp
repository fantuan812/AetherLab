#include "Startup/AetherStartupOverlay.h"
#include "Startup/AetherStartupClient.h"
#include "UI/AetherUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "CommonInputBaseTypes.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/CircularThrobber.h"
#include "InputCoreTypes.h"

void UAetherStartupActionButton::Present(EAetherStartupIntent Intent,FGuid Token,EAetherStartupStage Stage,bool Enabled)
{
    if(CurrentIntent!=Intent||CurrentToken!=Token||CurrentStage!=Stage||!Enabled)InvalidatePress();
    CurrentIntent=Intent;CurrentToken=Token;CurrentStage=Stage;SetIsEnabled(Enabled&&Token.IsValid());
    OnPressed.AddUniqueDynamic(this,&UAetherStartupActionButton::CapturePress);
    OnClicked.AddUniqueDynamic(this,&UAetherStartupActionButton::Dispatch);
}
void UAetherStartupActionButton::InvalidatePress(){PressedToken.Invalidate();bPressValid=false;}
void UAetherStartupActionButton::CapturePress()
{bPressValid=GetIsEnabled()&&CurrentToken.IsValid();PressedToken=bPressValid?CurrentToken:FGuid();}
void UAetherStartupActionButton::Dispatch()
{
    const bool Valid=bPressValid&&GetIsEnabled()&&CurrentToken.IsValid()&&PressedToken==CurrentToken;
    const FGuid Token=PressedToken;InvalidatePress();
    if(Valid)OnRequested.Broadcast(CurrentIntent,Token);
}
void UAetherStartupActionButton::ReleaseSlateResources(bool Children)
{InvalidatePress();Super::ReleaseSlateResources(Children);}

UAetherStartupOverlay::UAetherStartupOverlay()
{bIsModal=true;bIsBackHandler=true;bSupportsActivationFocus=true;SetIsFocusable(true);}
FString UAetherStartupOverlay::StageTitle(EAetherStartupStage Stage)
{
    switch(Stage)
    {
    case EAetherStartupStage::Idle:return {};
    case EAetherStartupStage::Frontend:return TEXT("AetherLab · 开始页");
    case EAetherStartupStage::Connecting:return TEXT("正在连接 / 载入");
    case EAetherStartupStage::WaitingForBackend:return TEXT("等待上一场景保存收尾");
    case EAetherStartupStage::ReadingStorage:return TEXT("正在读取世界存档");
    case EAetherStartupStage::Auditing:return TEXT("正在检查存档一致性");
    case EAetherStartupStage::Restoring:return TEXT("正在恢复世界");
    case EAetherStartupStage::WorldReady:return TEXT("世界已就绪 · 正在同步角色");
    case EAetherStartupStage::Ready:return TEXT("已就绪");
    case EAetherStartupStage::Failed:return TEXT("启动未完成");
    case EAetherStartupStage::Cancelled:return TEXT("已取消本次启动");
    }
    return TEXT("启动状态不可用");
}
FString UAetherStartupOverlay::FailureMessage(EAetherStartupFailure Failure)
{
    switch(Failure)
    {
    case EAetherStartupFailure::None:return {};
    case EAetherStartupFailure::InvalidConfiguration:return TEXT("启动配置无效。请检查配置后再试，未自动选择其他存档。");
    case EAetherStartupFailure::BackendConflict:return TEXT("已有世界仍在运行，不能同时启动另一份世界。");
    case EAetherStartupFailure::BackendDrainTimedOut:return TEXT("等待上一场景保存收尾超时。旧操作仍可能继续收尾，请勿删除存档。");
    case EAetherStartupFailure::StorageOpenFailed:return TEXT("无法打开世界存档。请保留原存档并检查服务器日志。");
    case EAetherStartupFailure::StorageAuditFailed:return TEXT("世界存档一致性检查未通过。请保留原存档并检查服务器日志。");
    case EAetherStartupFailure::WorldRestoreFailed:return TEXT("世界恢复未完成。请保留原存档并检查服务器日志。");
    case EAetherStartupFailure::ProfileUnavailable:return TEXT("角色资料尚未就绪，不能进入可操作世界。");
    case EAetherStartupFailure::NetworkFailure:return TEXT("当前连接未能完成。可在路由可用时显式重试原目标。");
    case EAetherStartupFailure::TravelFailure:return TEXT("地图切换未能完成。请检查目标资源，不会自动切换到其他世界。");
    }
    return TEXT("启动发生未知错误，请检查服务器日志。");
}
FString UAetherStartupOverlay::RouteMessage(EAetherStartupRouteIssue Issue)
{
    switch(Issue)
    {
    case EAetherStartupRouteIssue::None:return {};
    case EAetherStartupRouteIssue::FrontendUnavailable:return TEXT("开始页地图尚未验证，暂不能返回。");
    case EAetherStartupRouteIssue::PlayableUnavailable:return TEXT("可玩地图尚未验证，暂不能进入。");
    case EAetherStartupRouteIssue::NoRetryTarget:return TEXT("没有可安全重试的原目标。");
    case EAetherStartupRouteIssue::NoLocalWorld:return TEXT("本地地图上下文尚未就绪。");
    case EAetherStartupRouteIssue::Busy:return TEXT("当前阶段暂不接受此操作。");
    }
    return TEXT("当前没有安全可用的路由。");
}
TSharedRef<SWidget> UAetherStartupOverlay::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(!WidgetTree->RootWidget)
    {
        const auto& Theme=UAetherUITheme::Get();
        auto* Backdrop=WidgetTree->ConstructWidget<UBorder>();WidgetTree->RootWidget=Backdrop;
        Backdrop->SetBrushColor(Theme.Panel);Backdrop->SetPadding(FMargin(Theme.Padding*3));
        Backdrop->SetHorizontalAlignment(HAlign_Center);Backdrop->SetVerticalAlignment(VAlign_Center);
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetMaxDesiredWidth(640);Backdrop->SetContent(Size);
        auto* Rows=WidgetTree->ConstructWidget<UVerticalBox>();Size->SetContent(Rows);
        const auto AddText=[&](int32 FontSize,FLinearColor Color)
        {
            auto* Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetAutoWrapText(true);Text->SetColorAndOpacity(Color);
            auto Font=Text->GetFont();Font.Size=FontSize;Text->SetFont(Font);
            Rows->AddChildToVerticalBox(Text)->SetPadding(FMargin(0,Theme.Padding));return Text;
        };
        Title=AddText(Theme.BodySize+8,Theme.Accent);Detail=AddText(Theme.BodySize,Theme.Text);
        Progress=WidgetTree->ConstructWidget<UCircularThrobber>();Rows->AddChildToVerticalBox(Progress)->SetHorizontalAlignment(HAlign_Left);
        RouteDetail=AddText(Theme.BodySize,Theme.Text);
        auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();Rows->AddChildToVerticalBox(Actions);
        const auto Button=[&](const TCHAR* Label)
        {
            auto* Value=WidgetTree->ConstructWidget<UAetherStartupActionButton>();
            auto* Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetText(FText::FromString(Label));
            auto Font=Text->GetFont();Font.Size=Theme.BodySize;Text->SetFont(Font);Value->SetContent(Text);
            Value->OnRequested.AddUObject(this,&UAetherStartupOverlay::RequestIntent);
            Actions->AddChildToHorizontalBox(Value)->SetPadding(FMargin(0,Theme.Padding,Theme.Padding,0));return Value;
        };
        StartButton=Button(TEXT("进入世界"));RetryButton=Button(TEXT("重试原目标"));CancelButton=Button(TEXT("取消并返回开始页"));
    }
    Render();return Super::RebuildWidget();
}
void UAetherStartupOverlay::Present(UAetherStartupClient* InClient,const FAetherStartupView& InView)
{
    Client=InClient;View=InView;Render();
}
void UAetherStartupOverlay::Render()
{
    if(!Title||!Detail||!RouteDetail||!Progress||!StartButton||!CancelButton||!RetryButton)return;
    Title->SetText(FText::FromString(StageTitle(View.Stage)));
    FString Message=FailureMessage(View.FailureCode);
    if(Message.IsEmpty())switch(View.Stage)
    {
    case EAetherStartupStage::Frontend:Message=TEXT("准备好后，进入你的世界。");break;
    case EAetherStartupStage::Connecting:Message=TEXT("正在等待本地载入或服务器状态，请稍候。");break;
    case EAetherStartupStage::WaitingForBackend:Message=TEXT("已接受的操作正在收尾；当前启动不会抢占或关闭旧存储。");break;
    case EAetherStartupStage::WorldReady:Message=TEXT("正在同步当前角色资料，完成后即可继续游戏。");break;
    case EAetherStartupStage::Cancelled:Message=TEXT("本次启动已停止。已接受的保存操作不会因此回滚。");break;
    default:Message=TEXT("请稍候；页面只显示当前启动状态，不会自动重试。");break;
    }
    Detail->SetText(FText::FromString(Message));
    const bool Frontend=View.Stage==EAetherStartupStage::Frontend;
    const bool Terminal=View.Stage==EAetherStartupStage::Failed||View.Stage==EAetherStartupStage::Cancelled;
    const bool ShowRetry=View.bCanRetry||Terminal;
    const bool ShowCancel=!Frontend&&View.Stage!=EAetherStartupStage::Idle&&View.Stage!=EAetherStartupStage::Ready;
    Progress->SetVisibility(!Frontend&&!Terminal?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    const auto Configure=[&](UAetherStartupActionButton* Button,EAetherStartupIntent Intent,bool Visible,bool Enabled,EAetherStartupRouteIssue Issue)
    {
        Button->Present(Intent,View.LocalAttemptToken,View.Stage,Visible&&Enabled);
        Button->SetVisibility(Visible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        Button->SetToolTipText(FText::FromString(Enabled?FString():RouteMessage(Issue)));
    };
    Configure(StartButton,EAetherStartupIntent::Start,Frontend,View.bCanStart,View.StartIssue);
    Configure(RetryButton,EAetherStartupIntent::Retry,ShowRetry,View.bCanRetry,View.RetryIssue);
    Configure(CancelButton,EAetherStartupIntent::Cancel,ShowCancel,View.bCanCancel,View.CancelIssue);
    TArray<FString> Reasons;
    if(Frontend&&!View.bCanStart)Reasons.AddUnique(RouteMessage(View.StartIssue));
    if(ShowRetry&&!View.bCanRetry)Reasons.AddUnique(RouteMessage(View.RetryIssue));
    if(ShowCancel&&!View.bCanCancel)Reasons.AddUnique(RouteMessage(View.CancelIssue));
    RouteDetail->SetText(FText::FromString(FString::Join(Reasons,TEXT("\n"))));
    RouteDetail->SetVisibility(Reasons.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}
void UAetherStartupOverlay::RequestIntent(EAetherStartupIntent Intent,FGuid Token)
{
    if(!IsActivated()||!Client.IsValid()||Client->GetGameInstance()!=GetGameInstance())return;
    const auto& Current=Client->GetView();
    if(!Token.IsValid()||Token!=View.LocalAttemptToken||Token!=Current.LocalAttemptToken||View.Stage!=Current.Stage)return;
    InvalidateIntents();
    // 每次路由再次由客户端服务复验；回调可能立即 Travel，调用后不再访问本 Widget。
    if(Intent==EAetherStartupIntent::Start&&Current.bCanStart){Client->RequestStart(Token);return;}
    if(Intent==EAetherStartupIntent::Cancel&&Current.bCanCancel){Client->RequestCancel(Token);return;}
    if(Intent==EAetherStartupIntent::Retry&&Current.bCanRetry){Client->RequestRetry(Token);return;}
}
void UAetherStartupOverlay::InvalidateIntents()
{
    if(StartButton)StartButton->InvalidatePress();if(CancelButton)CancelButton->InvalidatePress();if(RetryButton)RetryButton->InvalidatePress();
}
void UAetherStartupOverlay::ClearPresentation()
{
    if(StartButton){StartButton->InvalidatePress();StartButton->SetIsEnabled(false);}
    if(CancelButton){CancelButton->InvalidatePress();CancelButton->SetIsEnabled(false);}
    if(RetryButton){RetryButton->InvalidatePress();RetryButton->SetIsEnabled(false);}
    Client.Reset();View={};
}
void UAetherStartupOverlay::NativeDestruct()
{ClearPresentation();Super::NativeDestruct();}
TOptional<FUIInputConfig> UAetherStartupOverlay::GetDesiredInputConfig() const
{
    FUIInputConfig Config(ECommonInputMode::Menu,EMouseCaptureMode::NoCapture,EMouseLockMode::DoNotLock,false);
    Config.bIgnoreMoveInput=true;Config.bIgnoreLookInput=true;return Config;
}
UWidget* UAetherStartupOverlay::NativeGetDesiredFocusTarget() const
{
    for(auto* Button:{StartButton.Get(),RetryButton.Get(),CancelButton.Get()})
        if(Button&&Button->GetIsEnabled()&&Button->IsVisible())return Button;
    return const_cast<UAetherStartupOverlay*>(this);
}
bool UAetherStartupOverlay::NativeOnHandleBackAction()
{if(View.bCanCancel)RequestIntent(EAetherStartupIntent::Cancel,View.LocalAttemptToken);return true;}
FReply UAetherStartupOverlay::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::Escape||Event.GetKey()==EKeys::Gamepad_FaceButton_Right)
    {if(!Event.IsRepeat())NativeOnHandleBackAction();return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}
