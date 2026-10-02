#pragma once
#include "CommonActivatableWidget.h"
#include "Components/Button.h"
#include "Startup/AetherStartupState.h"
#include "AetherStartupOverlay.generated.h"

class UAetherStartupClient;
class UTextBlock;
class UCircularThrobber;

enum class EAetherStartupIntent:uint8 {Start,Cancel,Retry};
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAetherStartupIntent,EAetherStartupIntent,FGuid);

// 按压绑定本地 attempt 与当前阶段；旧按钮松键不能操作后来建立的连接。
UCLASS()
class AETHERUI_API UAetherStartupActionButton : public UButton
{
    GENERATED_BODY()
public:
    void Present(EAetherStartupIntent Intent,FGuid Token,EAetherStartupStage Stage,bool Enabled);
    void InvalidatePress();
    virtual void ReleaseSlateResources(bool ReleaseChildren) override;
    FOnAetherStartupIntent OnRequested;
private:
    UFUNCTION() void CapturePress();
    UFUNCTION() void Dispatch();
    EAetherStartupIntent CurrentIntent=EAetherStartupIntent::Start;
    EAetherStartupStage CurrentStage=EAetherStartupStage::Idle;
    FGuid CurrentToken,PressedToken;
    bool bPressValid=false;
};

// 独立 CommonUI 激活根，不依赖 HUD、Pawn 或 GameState，也不修改输入模式或存储。
UCLASS()
class AETHERUI_API UAetherStartupOverlay : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    UAetherStartupOverlay();
    void Present(UAetherStartupClient* Client,const FAetherStartupView& View);
    void ClearPresentation();
    void InvalidateIntents();
    static FString StageTitle(EAetherStartupStage Stage);
    static FString FailureMessage(EAetherStartupFailure Failure);
    static FString RouteMessage(EAetherStartupRouteIssue Issue);
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeDestruct() override;
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
    virtual bool NativeOnHandleBackAction() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    friend class FAetherStartupPresentationTest;
    void Render();
    void RequestIntent(EAetherStartupIntent Intent,FGuid Token);
    TWeakObjectPtr<UAetherStartupClient> Client;
    FAetherStartupView View;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Detail;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RouteDetail;
    UPROPERTY(Transient) TObjectPtr<UCircularThrobber> Progress;
    UPROPERTY(Transient) TObjectPtr<UAetherStartupActionButton> StartButton;
    UPROPERTY(Transient) TObjectPtr<UAetherStartupActionButton> CancelButton;
    UPROPERTY(Transient) TObjectPtr<UAetherStartupActionButton> RetryButton;
};
