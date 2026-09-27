#pragma once
#include "CommonActivatableWidget.h"
#include "AetherRecoveryLayer.generated.h"
class AAetherFrontierCharacter;
class UTextBlock;
class UAetherPageButton;

// 每次倒地绑定 Pawn 和服务端生命令牌；不持有任何客户端恢复权限。
UCLASS()
class AETHERUI_API UAetherRecoveryLayer : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    UAetherRecoveryLayer();
    void Refresh(AAetherFrontierCharacter* Pawn);
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E) override;
protected:
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
    virtual bool NativeOnHandleBackAction() override{return true;}
private:
    void RequestRecovery();
    TWeakObjectPtr<AAetherFrontierCharacter> BoundPawn;
    FGuid Life;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UAetherPageButton> WaitButton;
    UPROPERTY() TObjectPtr<UAetherPageButton> RecoverButton;
};
