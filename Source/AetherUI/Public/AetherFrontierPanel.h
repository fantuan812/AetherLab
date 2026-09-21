#pragma once
#include "CommonActivatableWidget.h"
#include "Input/UIActionBindingHandle.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "AetherFrontierPanel.generated.h"
class AAetherFrontierCharacter;
class AAetherPlayerState;
class UAetherCommandClient;
class UWidgetSwitcher;
class UHorizontalBox;
class UTextBlock;
class UAetherInventoryPage;
class UAetherSkillTreePage;
class UAetherPageBase;
UCLASS()
class AETHERUI_API UAetherFrontierViewModel : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly) FText Heading;
    UPROPERTY(BlueprintReadOnly) FText Body;
    uint64 RefreshCount=0;
    void Refresh(AAetherFrontierCharacter* Character);
};
/** 菜单壳只处理页签、焦点与输入；物品、技能、任务、地图、队伍、设置分别拥有页面控制器。 */
UCLASS()
class AETHERUI_API UAetherFrontierPanel : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
    virtual UWidget* NativeGetDesiredFocusTarget() const override{return GetPrimaryFocusTarget();}
    void RefreshSnapshot();
    UWidget* GetPrimaryFocusTarget() const;
    UPROPERTY() TObjectPtr<UAetherFrontierViewModel> Model;
private:
    void HandleMenuChanged();
    void BindProfile();
    TWeakObjectPtr<UAetherMenuSubsystem> Menu;
    TWeakObjectPtr<UAetherCommandClient> Client;
    TWeakObjectPtr<AAetherPlayerState> BoundProfile;
    EAetherMenuPage ShownPage=EAetherMenuPage::None;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidgetSwitcher> PageHost;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UHorizontalBox> Tabs;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Title;
    UPROPERTY() TMap<EAetherMenuPage,TObjectPtr<UUserWidget>> Pages;
};
