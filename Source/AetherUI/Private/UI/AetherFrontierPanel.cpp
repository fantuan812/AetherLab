#include "AetherFrontierPanel.h"
#include "UI/AetherWidgetAssets.h"
#include "UI/AetherPageBase.h"
#include "Inventory/AetherInventoryPage.h"
#include "Skills/AetherSkillTreePage.h"
#include "Journal/AetherJournalPage.h"
#include "Map/AetherMapPage.h"
#include "Party/AetherPartyPage.h"
#include "Settings/AetherSettingsPage.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Networking/AetherCommandClient.h"
#include "CommonInputBaseTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
namespace
{
const TCHAR* PageTitle(EAetherMenuPage P)
{
    switch(P){case EAetherMenuPage::Inventory:return TEXT("背包与装备");case EAetherMenuPage::Journal:return TEXT("任务日志");
    case EAetherMenuPage::Skills:return TEXT("技能成长");case EAetherMenuPage::Map:return TEXT("世界地图");case EAetherMenuPage::Party:return TEXT("队伍");
    case EAetherMenuPage::System:return TEXT("设置");default:return TEXT("AetherLab");}
}
}
void UAetherFrontierViewModel::Refresh(AAetherFrontierCharacter* C)
{
    ++RefreshCount;Heading=FText::FromString(C?PageTitle(EAetherMenuPage(C->Panel)):TEXT("AetherLab"));
    // 保留诊断快照接口，正式页面不把这个字符串当正文布局。
    Body=FText::GetEmpty();
}
TSharedRef<SWidget> UAetherFrontierPanel::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(!Model)Model=NewObject<UAetherFrontierViewModel>(this);
    if(!WidgetTree->RootWidget)
    {
        auto* Frame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("MenuFrame"));
        Frame->SetPadding(FMargin(20));Frame->SetBrushColor(FLinearColor(.015,.025,.04,.98));WidgetTree->RootWidget=Frame;
        auto* Rows=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("MenuRows"));Frame->SetContent(Rows);
        Title=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("Title"));Title->SetColorAndOpacity(FLinearColor(1,.8,.4));Rows->AddChildToVerticalBox(Title);
        Tabs=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("Tabs"));Rows->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0,10));
        PageHost=WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(),TEXT("PageHost"));Rows->AddChildToVerticalBox(PageHost)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
    else
    {
        Title=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("Title")));Tabs=Cast<UHorizontalBox>(WidgetTree->FindWidget(TEXT("Tabs")));
        PageHost=Cast<UWidgetSwitcher>(WidgetTree->FindWidget(TEXT("PageHost")));
    }
    if(!Title||!Tabs||!PageHost)return Super::RebuildWidget();
    if(Pages.IsEmpty())
    {
        struct FPage{EAetherMenuPage Id;UClass* Class;};
        const FPage Definitions[]={{EAetherMenuPage::Inventory,UAetherInventoryPage::StaticClass()},{EAetherMenuPage::Journal,UAetherJournalPage::StaticClass()},
            {EAetherMenuPage::Skills,UAetherSkillTreePage::StaticClass()},{EAetherMenuPage::Map,UAetherMapPage::StaticClass()},
            {EAetherMenuPage::Party,UAetherPartyPage::StaticClass()},{EAetherMenuPage::System,UAetherSettingsPage::StaticClass()}};
        for(const auto& D:Definitions)
        {
            auto* W=CreateWidget<UUserWidget>(GetOwningPlayer(),AetherWidgetAssets::Resolve(D.Class));Pages.Add(D.Id,W);PageHost->AddChild(W);
            auto* B=WidgetTree->ConstructWidget<UAetherPageButton>();auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(PageTitle(D.Id)));B->SetContent(T);
            B->Bind(FSimpleDelegate::CreateWeakLambda(this,[this,Id=D.Id](){if(Menu.IsValid()&&Menu->GetLayerCount()==0)Menu->OpenPage(Id);}));
            auto* SlotValue=Tabs->AddChildToHorizontalBox(B);SlotValue->SetSize(FSlateChildSize(ESlateSizeRule::Fill));SlotValue->SetPadding(FMargin(3));
        }
        auto* Close=WidgetTree->ConstructWidget<UAetherPageButton>();auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(TEXT("关闭")));Close->SetContent(T);
        Close->Bind(FSimpleDelegate::CreateWeakLambda(this,[this](){if(Menu.IsValid())Menu->Back();}));Tabs->AddChildToHorizontalBox(Close);
    }
    return Super::RebuildWidget();
}
void UAetherFrontierPanel::NativeConstruct()
{
    Super::NativeConstruct();SetIsFocusable(true);
    if(auto* LP=GetOwningLocalPlayer())
    {
        Menu=LP->GetSubsystem<UAetherMenuSubsystem>();Client=LP->GetSubsystem<UAetherCommandClient>();
        Menu->OnChanged.AddUObject(this,&UAetherFrontierPanel::HandleMenuChanged);Client->OnChanged.AddUObject(this,&UAetherFrontierPanel::RefreshSnapshot);
    }
    HandleMenuChanged();
}
void UAetherFrontierPanel::BindProfile()
{
    auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());auto* PS=C?C->ProfileState():nullptr;
    if(PS==BoundProfile.Get())return;if(BoundProfile.IsValid())BoundProfile->OnProfilePublished.RemoveAll(this);
    BoundProfile=PS;if(PS)PS->OnProfilePublished.AddUObject(this,&UAetherFrontierPanel::RefreshSnapshot);
}
void UAetherFrontierPanel::HandleMenuChanged()
{
    if(!Menu.IsValid()||!PageHost)return;const auto Page=Menu->GetPage();
    if(const auto* W=Pages.Find(Page))
    {
        const bool Changed=ShownPage!=Page;ShownPage=Page;PageHost->SetActiveWidget(W->Get());Title->SetText(FText::FromString(PageTitle(Page)));
        if(Changed){RefreshSnapshot();if(Menu->GetLayerCount()==0)if(auto* Focus=GetPrimaryFocusTarget())Focus->SetUserFocus(GetOwningPlayer());}
    }
}
void UAetherFrontierPanel::RefreshSnapshot()
{
    BindProfile();auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());if(Model)Model->Refresh(C);
    if(auto* W=Pages.Find(EAetherMenuPage::Skills))if(auto* S=Cast<UAetherSkillTreePage>(W->Get()))S->SetLegacySource(ShownPage==EAetherMenuPage::Skills?C:nullptr);
}
UWidget* UAetherFrontierPanel::GetPrimaryFocusTarget() const
{
    const auto* P=Pages.Find(ShownPage);auto* W=P?P->Get():nullptr;if(!W)return const_cast<UAetherFrontierPanel*>(this);
    if(auto* I=Cast<UAetherInventoryPage>(W))return I->GetNavigationFocusTarget();
    if(auto* S=Cast<UAetherSkillTreePage>(W))return S->GetNavigationFocusTarget();
    if(auto* B=Cast<UAetherPageBase>(W))return B->InitialFocus();return W;
}
FReply UAetherFrontierPanel::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(!Menu.IsValid()||!Menu->IsOpen())return Super::NativeOnPreviewKeyDown(G,E);
    const auto* W=Pages.Find(EAetherMenuPage::System);const auto* Settings=W?Cast<UAetherSettingsPage>(W->Get()):nullptr;
    if(Settings&&Settings->IsSelectingKey())return Super::NativeOnPreviewKeyDown(G,E);
    if(Menu->GetLayerCount()==0&&(E.GetKey()==EKeys::Gamepad_LeftShoulder||E.GetKey()==EKeys::Gamepad_RightShoulder))
    {
        const EAetherMenuPage Order[]={EAetherMenuPage::Inventory,EAetherMenuPage::Journal,EAetherMenuPage::Skills,EAetherMenuPage::Map,EAetherMenuPage::Party,EAetherMenuPage::System};
        if(!E.IsRepeat())for(int32 I=0;I<6;++I)if(Order[I]==ShownPage){Menu->OpenPage(Order[(I+(E.GetKey()==EKeys::Gamepad_RightShoulder?1:5))%6]);break;}
        return FReply::Handled();
    }
    // 只有返回键交给页面先关闭详情；全局切页键不能被整页无条件跳过。
    const bool Back=E.GetKey()==EKeys::Escape||E.GetKey()==EKeys::Gamepad_FaceButton_Right;
    if(Back&&(ShownPage==EAetherMenuPage::Inventory||ShownPage==EAetherMenuPage::Skills))return Super::NativeOnPreviewKeyDown(G,E);
    const auto Focused=FSlateApplication::Get().GetKeyboardFocusedWidget();
    if(!Back&&Focused.IsValid()&&Focused->GetTypeAsString().Contains(TEXT("EditableText")))return Super::NativeOnPreviewKeyDown(G,E);
    if(E.GetKey()==EKeys::Escape||E.GetKey()==EKeys::Gamepad_FaceButton_Right){if(!E.IsRepeat())Menu->Back();return FReply::Handled();}
    if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn()))
    {
        const TPair<FName,EAetherMenuPage> Keys[]={{"I",EAetherMenuPage::Inventory},{"J",EAetherMenuPage::Journal},{"K",EAetherMenuPage::Skills},{"M",EAetherMenuPage::Map},{"P",EAetherMenuPage::Party}};
        for(const auto& Pair:Keys)if(E.GetKey()==C->BindingFor(Pair.Key)){if(!E.IsRepeat())Menu->TogglePage(Pair.Value);return FReply::Handled();}
    }
    return Super::NativeOnPreviewKeyDown(G,E);
}
TOptional<FUIInputConfig> UAetherFrontierPanel::GetDesiredInputConfig() const
{FUIInputConfig C(ECommonInputMode::Menu,EMouseCaptureMode::NoCapture,EMouseLockMode::DoNotLock,false);C.bIgnoreMoveInput=true;C.bIgnoreLookInput=true;return C;}
void UAetherFrontierPanel::NativeDestruct()
{
    if(Menu.IsValid())Menu->OnChanged.RemoveAll(this);if(Client.IsValid())Client->OnChanged.RemoveAll(this);
    if(BoundProfile.IsValid())BoundProfile->OnProfilePublished.RemoveAll(this);
    Menu.Reset();Client.Reset();BoundProfile.Reset();ShownPage=EAetherMenuPage::None;Super::NativeDestruct();
}
