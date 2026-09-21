#include "UI/AetherPlayerHUDWidget.h"
#include "UI/AetherPageWidgets.h"
#include "AetherFrontier.h"
#include "Networking/AetherCommandClient.h"
#include "Definitions/AetherV10Definitions.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "TimerManager.h"
using namespace AetherPageWidgets;
TSharedRef<SWidget> UAetherPlayerHUDWidget::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(!WidgetTree->RootWidget)
    {
        auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;
        auto Panel=[&](FVector2D Anchor,FVector2D Align,FVector2D Position,FVector2D Size)
        {
            auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrushColor(FLinearColor(.018,.03,.045,.9));Border->SetPadding(FMargin(12));
            auto* Slot=Root->AddChildToCanvas(Border);Slot->SetAnchors(FAnchors(Anchor.X,Anchor.Y));Slot->SetAlignment(Align);Slot->SetPosition(Position);Slot->SetSize(Size);
            auto* Rows=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Rows);return Rows;
        };
        auto* Resources=Panel(FVector2D(0,0),FVector2D(0,0),FVector2D(20,20),FVector2D(300,155));
        const TCHAR* Names[]={TEXT("生命"),TEXT("法力"),TEXT("体力")};const FLinearColor Colors[]={FLinearColor(.8,.23,.2),FLinearColor(.22,.5,.9),FLinearColor(.2,.75,.45)};
        for(int32 I=0;I<3;++I)
        {
            Vitals.Add(Text(*WidgetTree,*Resources,Names[I]));auto* B=WidgetTree->ConstructWidget<UProgressBar>();B->SetFillColorAndOpacity(Colors[I]);Resources->AddChildToVerticalBox(B);Bars.Add(B);
        }
        auto* Quest=Panel(FVector2D(1,0),FVector2D(1,0),FVector2D(-20,20),FVector2D(280,190));
        Guidance=Text(*WidgetTree,*Quest,TEXT(""),FLinearColor(1,.82,.45));
        auto* Hotbar=Panel(FVector2D(.5,1),FVector2D(.5,1),FVector2D(0,-20),FVector2D(500,98));
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Hotbar->AddChildToVerticalBox(Row);
        for(int32 I=0;I<4;++I)
        {
            auto* Col=WidgetTree->ConstructWidget<UVerticalBox>();Row->AddChildToHorizontalBox(Col)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            Skills.Add(Text(*WidgetTree,*Col,FString::FromInt(I+1),FLinearColor(.9,.8,.5)));
        }
        State=Text(*WidgetTree,*Hotbar,TEXT(""));
        auto* Middle=Panel(FVector2D(.5,.68),FVector2D(.5,.5),FVector2D::ZeroVector,FVector2D(520,130));
        Interaction=Text(*WidgetTree,*Middle,TEXT(""),FLinearColor(1,.85,.45));Feedback=Text(*WidgetTree,*Middle,TEXT(""));
        Encounter=Text(*WidgetTree,*Middle,TEXT(""),FLinearColor(.6,.8,1));
        Crosshair=WidgetTree->ConstructWidget<UBorder>();Crosshair->SetBrushColor(FLinearColor(.9,.95,1,.8));
        auto* Dot=Root->AddChildToCanvas(Crosshair);Dot->SetAnchors(FAnchors(.5,.5));Dot->SetAlignment(FVector2D(.5,.5));Dot->SetSize(FVector2D(4,4));
    }
    return Super::RebuildWidget();
}
void UAetherPlayerHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();SetVisibility(ESlateVisibility::HitTestInvisible);Refresh();
    GetWorld()->GetTimerManager().SetTimer(Timer,this,&UAetherPlayerHUDWidget::Refresh,.1f,true);
}
void UAetherPlayerHUDWidget::NativeDestruct()
{if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(Timer);Super::NativeDestruct();}
void UAetherPlayerHUDWidget::Refresh()
{
    auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());auto* LP=GetOwningLocalPlayer();
    if(!C||!LP||Bars.Num()!=3)return;
    const bool MenuOpen=LP->GetSubsystem<UAetherMenuSubsystem>()->IsOpen();
    SetVisibility(MenuOpen?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    if(MenuOpen)return;
    const float Value[]={C->Health(),C->Mana(),C->Stamina()},Max[]={C->MaxHealth,C->MaximumMana(),C->MaximumStamina()};
    const TCHAR* Names[]={TEXT("生命"),TEXT("法力"),TEXT("体力")};
    for(int32 I=0;I<3;++I)
    {Bars[I]->SetPercent(FMath::Clamp(Value[I]/FMath::Max(1.f,Max[I]),0.f,1.f));Vitals[I]->SetText(FText::FromString(FString::Printf(TEXT("%s %.0f / %.0f"),Names[I],Value[I],Max[I])));}
    const auto* Net=LP->GetSubsystem<UAetherCommandClient>();const auto& Profile=Net->GetProfile();
    const auto& Definitions=FAetherV10Definitions::Get().Skills;
    for(int32 I=0;I<Skills.Num();++I)
    {
        const auto* Id=Profile.IsSet()?Profile->Skills.Hotbar.Find(I):nullptr;const auto* D=Id?Definitions.Skills.Find(*Id):nullptr;
        Skills[I]->SetText(FText::FromString(FString::Printf(TEXT("%s%d %s%s"),C->SelectedSpell==I?TEXT("▸ "):TEXT(""),I+1,D?*D->DisplayName:TEXT("未绑定"),D&&!C->SkillUnlocked(*Id)?TEXT(" · 未授权"):TEXT(""))));
    }
    const auto G=AetherGuide::Resolve(C);
    Guidance->SetText(FText::FromString(G.Title+LINE_TERMINATOR+G.Label+LINE_TERMINATOR+G.Hint+
        (G.bHasTarget?LINE_TERMINATOR+FString::Printf(TEXT("目标 %.0f 米 · J 查看"),FVector::Dist2D(G.Position,C->GetActorLocation())/100):FString())));
    C->RefreshInteractionFocus();Interaction->SetText(FText::FromString(C->InteractionFocus.Prompt));
    if(C->Feedback!=LastFeedback){LastFeedback=C->Feedback;FeedbackUntil=GetWorld()->GetTimeSeconds()+6;}
    Feedback->SetText(FText::FromString(GetWorld()->GetTimeSeconds()<FeedbackUntil?LastFeedback:FString()));
    FString Status;
    if(!C->Alive())Status=TEXT("倒地 · 队友可救援，稍后可回据点");
    else if(C->ReviveTarget)Status=TEXT("救援中 · 保持靠近，受伤会打断");
    else if(C->Carried)Status=TEXT("搬运中 · G 放下 / C 投掷");
    else if(C->bIsCrouched)Status=TEXT("蹲姿");
    else if(C->bBlocking)Status=TEXT("格挡");
    else if(C->CombatTime()<C->StunUntil)Status=TEXT("眩晕");
    State->SetText(FText::FromString(Status));FString Activity;
    for(TActorIterator<AAetherEncounterDirector> It(GetWorld());It;++It)for(const auto* Run:{&It->Abbey,&It->Relay})
        if(Run->Phase!=EAetherEncounterPhase::Idle)Activity+=FString::Printf(TEXT("%s · 第 %d 波 · 引导 %.1f 秒  "),*Run->Definition.ToString(),Run->Wave+1,Run->Progress);
    Encounter->SetText(FText::FromString(Activity));
}
