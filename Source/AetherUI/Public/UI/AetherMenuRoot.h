#pragma once
#include "CommonActivatableWidget.h"
#include "Input/UIActionBindingHandle.h"
#include "AetherMenuRoot.generated.h"
class UCommonActivatableWidgetStack;
class UBorder;
class UAetherFrontierPanel;
class UAetherDialoguePage;
class UAetherMenuSubsystem;
/** 底层游戏输入节点；菜单和模态节点激活时由 CommonUI 自动覆盖。 */
UCLASS()
class AETHERUI_API UAetherGameInputLayer : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    UAetherGameInputLayer();
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
};
UCLASS()
class AETHERUI_API UAetherModalLayer : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    UAetherModalLayer();
    FGuid Token;
    void SetBody(UUserWidget* Body);
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override{return FReply::Handled();}
protected:
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
    virtual bool NativeOnHandleBackAction() override;
private:
    UPROPERTY() TObjectPtr<UBorder> Shield;
    UPROPERTY() TObjectPtr<UUserWidget> BodyWidget;
};
/** 一个 LocalPlayer 一个根。主菜单、确认弹层处在同一 CommonUI 输入树内。 */
UCLASS()
class AETHERUI_API UAetherMenuRoot : public UUserWidget
{
    GENERATED_BODY()
public:
    static UAetherMenuRoot* Find(const UUserWidget& Context);
    bool PushModal(FGuid Token,UUserWidget* Body);
    void PopModal(FGuid Token);
    UAetherFrontierPanel* GetPanel() const{return Panel;}
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    void Refresh();
    TWeakObjectPtr<UAetherMenuSubsystem> Menu;
    UPROPERTY() TObjectPtr<UAetherGameInputLayer> GameLayer;
    UPROPERTY() TObjectPtr<UCommonActivatableWidgetStack> MainStack;
    UPROPERTY() TObjectPtr<UCommonActivatableWidgetStack> ModalStack;
    UPROPERTY() TObjectPtr<UAetherFrontierPanel> Panel;
    UPROPERTY() TObjectPtr<UAetherDialoguePage> Dialogue;
    UPROPERTY() TMap<FGuid,TObjectPtr<UAetherModalLayer>> Modals;
};
