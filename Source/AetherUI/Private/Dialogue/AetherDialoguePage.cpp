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
{return AetherInteractionQueries::ReasonText(C.ReasonId,C.ReasonParameters,FAetherV10Definitions::Get().Rules);}
}
void UAetherDialogueChoiceButton::BindChoice(int32 Index,uint64 Version)
{ChoiceIndex=Index;ShownVersion=Version;OnClicked.AddUniqueDynamic(this,&UAetherDialogueChoiceButton::Select);}
void UAetherDialogueChoiceButton::Select(){OnChoice.ExecuteIfBound(ChoiceIndex,ShownVersion);}
void UAetherDialoguePlaybackButton::BindPlayback(bool Skip,uint64 Version)
{bSkip=Skip;ShownVersion=Version;OnClicked.AddUniqueDynamic(this,&UAetherDialoguePlaybackButton::ActivatePlayback);}
void UAetherDialoguePlaybackButton::ActivatePlayback(){OnPlayback.ExecuteIfBound(bSkip,ShownVersion);}
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
    if(!View||!Choices){PreferredFocus.Reset();return;}
    auto* Current=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());
    if(Player.Get()!=Current){if(Player.IsValid())Player->OnPresentationChanged.RemoveAll(this);Player=Current;if(Current)Current->OnPresentationChanged.AddUObject(this,&UAetherDialoguePage::RefreshFeedback);}
    int32 FocusIndex=INDEX_NONE;
    for(int32 I=0;I<Choices->GetChildrenCount();++I)if(Choices->GetChildAt(I)->HasUserFocus(GetOwningPlayer()))FocusIndex=I;
    Speaker->SetText(FText::FromString(View->Speaker));Speech->SetText(FText::FromString(Session->GetSubtitle()));Choices->ClearChildren();
    UWidget* Focus=nullptr;
    if(Session->GetPlaybackPhase()==EAetherDialoguePlaybackPhase::Speaking)
    {
        if(const auto* P=Session->GetPresentation())
        {
            const auto Add=[&](bool Skip,const FString& Caption)
            {
                auto* B=WidgetTree->ConstructWidget<UAetherDialoguePlaybackButton>();B->BindPlayback(Skip,Session->GetVersion());B->OnPlayback.BindUObject(this,&UAetherDialoguePage::Playback);
                auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetText(FText::FromString(Caption));B->SetContent(Label);
                Choices->AddChildToVerticalBox(B)->SetPadding(FMargin(0,5));if(!Focus)Focus=B;
            };
            if(P->bAllowAdvance)Add(false,P->AdvanceLabel);if(P->bAllowSkip)Add(true,P->SkipLabel);
        }
    }
    else for(int32 I=0;I<View->Choices.Num();++I)
    {
        const auto& Choice=View->Choices[I];auto* B=WidgetTree->ConstructWidget<UAetherDialogueChoiceButton>();
        B->BindChoice(I,Session->GetVersion());B->OnChoice.BindUObject(this,&UAetherDialoguePage::Choose);
        const bool Enabled=Choice.Availability==EAetherOfferAvailability::Available||Choice.Availability==EAetherOfferAvailability::TalkOnly;
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetAutoWrapText(true);
        Label->SetText(FText::FromString(Choice.Label+(Enabled?FString():TEXT(" · ")+DisabledReason(Choice))));B->SetContent(Label);B->SetIsEnabled(Enabled);
        Choices->AddChildToVerticalBox(B)->SetPadding(FMargin(0,5));
        if(Enabled&&(!Focus||I==FocusIndex))Focus=B;
    }
    PreferredFocus=Focus;
    if((Opening||FocusIndex!=INDEX_NONE)&&Focus)Focus->SetUserFocus(GetOwningPlayer());
}
void UAetherDialoguePage::Playback(bool Skip,uint64 Version)
{if(Session.IsValid()){if(Skip)Session->Skip(Version);else Session->Advance(Version);}}
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

UWidget* UAetherDialoguePage::NativeGetDesiredFocusTarget() const
{return PreferredFocus.IsValid()?PreferredFocus.Get():const_cast<UAetherDialoguePage*>(this);}
