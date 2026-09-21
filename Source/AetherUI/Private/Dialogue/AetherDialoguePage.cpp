#include "Dialogue/AetherDialoguePage.h"
#include "UI/AetherWidgetAssets.h"
#include "CommonInputBaseTypes.h"
#include "Interaction/AetherDialogueSession.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Definitions/AetherV10Definitions.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Engine/LocalPlayer.h"
#include "InputCoreTypes.h"

namespace
{
FString DisabledReason(const FAetherDialogueChoiceView& C)
{
    if(C.ReasonId==TEXT("QuestRequired"))
    {
        const auto* Id=C.ReasonParameters.Find(TEXT("QuestId"));
        const auto* Q=Id?FAetherV10Definitions::Get().Rules.Quest(FName(**Id)):nullptr;
        return TEXT("先完成：")+(Q?Q->Title:Id?*Id:TEXT("前置任务"));
    }
    if(C.ReasonId==TEXT("ObjectiveRequired"))return TEXT("先完成前置目标");
    if(C.ReasonId==TEXT("ObjectivesIncomplete"))return TEXT("任务目标尚未完成");
    if(C.ReasonId==TEXT("InCombat"))return TEXT("脱离战斗后开放");
    if(C.ReasonId==TEXT("TargetThreatened"))return TEXT("附近仍有威胁");
    if(C.ReasonId==TEXT("OutOfReach"))return TEXT("请靠近并保持视线");
    if(C.ReasonId==TEXT("TargetBusy"))return TEXT("请先结束当前动作");
    if(C.ReasonId==TEXT("QuestUnavailable"))return TEXT("任务已完成或尚未开放");
    return TEXT("暂不可用，请等待状态更新");
}
}
void UAetherDialogueChoiceButton::BindChoice(int32 Index,uint64 Version)
{ChoiceIndex=Index;ShownVersion=Version;OnClicked.AddUniqueDynamic(this,&UAetherDialogueChoiceButton::Select);}
void UAetherDialogueChoiceButton::Select(){OnChoice.ExecuteIfBound(ChoiceIndex,ShownVersion);}
TSharedRef<SWidget> UAetherDialoguePage::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(WidgetTree->RootWidget)AetherWidgetAssets::BindDesigner(*this,*WidgetTree);
    if(!WidgetTree->RootWidget)
    {
        auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetPadding(FMargin(28));Border->SetBrushColor(FLinearColor(.025f,.035f,.055f,.98f));WidgetTree->RootWidget=Border;
        auto* Rows=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Rows);
        Speaker=WidgetTree->ConstructWidget<UTextBlock>();Speaker->SetColorAndOpacity(FLinearColor(1,.8f,.4f));Rows->AddChildToVerticalBox(Speaker);
        Speech=WidgetTree->ConstructWidget<UTextBlock>();Speech->SetAutoWrapText(true);Rows->AddChildToVerticalBox(Speech)->SetPadding(FMargin(0,18));
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Rows->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Choices=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Choices);
        Feedback=WidgetTree->ConstructWidget<UTextBlock>();Feedback->SetAutoWrapText(true);Rows->AddChildToVerticalBox(Feedback);
    }
    return Super::RebuildWidget();
}
void UAetherDialoguePage::NativeConstruct()
{
    Super::NativeConstruct();SetIsFocusable(true);
    // 嵌入根栈；不再单独挂到 Viewport。
    if(auto* LP=GetOwningLocalPlayer()){Session=LP->GetSubsystem<UAetherDialogueSession>();Session->OnChanged.AddUObject(this,&UAetherDialoguePage::Refresh);}
    Player=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());if(Player.IsValid())Player->OnPresentationChanged.AddUObject(this,&UAetherDialoguePage::RefreshFeedback);
    Refresh();
}
void UAetherDialoguePage::RefreshFeedback(){if(Feedback&&Player.IsValid())Feedback->SetText(FText::FromString(Player->Feedback));}
void UAetherDialoguePage::NativeDestruct()
{if(Player.IsValid())Player->OnPresentationChanged.RemoveAll(this);Player.Reset();if(Session.IsValid())Session->OnChanged.RemoveAll(this);Session.Reset();Super::NativeDestruct();}
void UAetherDialoguePage::Refresh()
{
    const auto* View=Session.IsValid()&&Session->GetView().IsSet()?&Session->GetView().GetValue():nullptr;
    const bool Opening=GetVisibility()==ESlateVisibility::Collapsed;SetVisibility(View?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(!View||!Choices)return;
    auto* Current=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());
    if(Player.Get()!=Current){if(Player.IsValid())Player->OnPresentationChanged.RemoveAll(this);Player=Current;if(Current)Current->OnPresentationChanged.AddUObject(this,&UAetherDialoguePage::RefreshFeedback);}
    int32 FocusIndex=INDEX_NONE;
    for(int32 I=0;I<Choices->GetChildrenCount();++I)if(Choices->GetChildAt(I)->HasUserFocus(GetOwningPlayer()))FocusIndex=I;
    Speaker->SetText(FText::FromString(View->Speaker));Speech->SetText(FText::FromString(View->Text));Choices->ClearChildren();
    UWidget* Focus=nullptr;
    for(int32 I=0;I<View->Choices.Num();++I)
    {
        const auto& Choice=View->Choices[I];auto* B=WidgetTree->ConstructWidget<UAetherDialogueChoiceButton>();
        B->BindChoice(I,Session->GetVersion());B->OnChoice.BindUObject(this,&UAetherDialoguePage::Choose);
        const bool Enabled=Choice.Availability==EAetherOfferAvailability::Available||Choice.Availability==EAetherOfferAvailability::TalkOnly;
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetAutoWrapText(true);
        Label->SetText(FText::FromString(Choice.Label+(Enabled?FString():TEXT(" · ")+DisabledReason(Choice))));B->SetContent(Label);B->SetIsEnabled(Enabled);
        Choices->AddChildToVerticalBox(B)->SetPadding(FMargin(0,5));
        if(Enabled&&(!Focus||I==FocusIndex))Focus=B;
    }
    if((Opening||FocusIndex!=INDEX_NONE)&&Focus)Focus->SetUserFocus(GetOwningPlayer());
}
void UAetherDialoguePage::Choose(int32 Index,uint64 Version)
{
    FString Why;if(Session.IsValid())Session->Choose(Index,Version,Why);
    if(Feedback)Feedback->SetText(FText::FromString(Why));
}
FReply UAetherDialoguePage::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Escape||E.GetKey()==EKeys::Gamepad_FaceButton_Right){if(Session.IsValid())Session->Close();return FReply::Handled();}
    return Super::NativeOnKeyDown(G,E);
}

TOptional<FUIInputConfig> UAetherDialoguePage::GetDesiredInputConfig() const
{FUIInputConfig C(ECommonInputMode::Menu,EMouseCaptureMode::NoCapture,EMouseLockMode::DoNotLock,false);C.bIgnoreMoveInput=true;C.bIgnoreLookInput=true;return C;}
