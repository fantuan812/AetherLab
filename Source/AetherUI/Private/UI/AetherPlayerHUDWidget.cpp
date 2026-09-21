#include "UI/AetherPlayerHUDWidget.h"
#include "UI/AetherHUDSection.h"
#include "Inventory/AetherNativeInventory.h"
#include "UI/AetherWidgetAssets.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
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
        auto Panel=[&](const TCHAR* Asset,FVector2D Anchor,FVector2D Align,FVector2D Position,FVector2D Size)
        {
            auto* Border=CreateWidget<UAetherHUDSection>(GetOwningPlayer(),AetherWidgetAssets::Class<UAetherHUDSection>(Asset));
            auto* Slot=Root->AddChildToCanvas(Border);Slot->SetAnchors(FAnchors(Anchor.X,Anchor.Y));Slot->SetAlignment(Align);Slot->SetPosition(Position);Slot->SetSize(Size);
            return Border->GetRows();
        };
        auto* Resources=Panel(TEXT("WBP_Vitals"),FVector2D(0,0),FVector2D(0,0),FVector2D(20,20),FVector2D(320,225));
        const TCHAR* Names[]={TEXT("生命"),TEXT("法力"),TEXT("体力")};const FLinearColor Colors[]={FLinearColor(.8,.23,.2),FLinearColor(.22,.5,.9),FLinearColor(.2,.75,.45)};
        for(int32 I=0;I<3;++I)
        {
            Vitals.Add(Text(*WidgetTree,*Resources,Names[I]));auto* B=WidgetTree->ConstructWidget<UProgressBar>();B->SetFillColorAndOpacity(Colors[I]);Resources->AddChildToVerticalBox(B);Bars.Add(B);
        }
        EffectsRow=WidgetTree->ConstructWidget<UHorizontalBox>();Resources->AddChildToVerticalBox(EffectsRow);
        auto* Quest=Panel(TEXT("WBP_QuestTracker"),FVector2D(1,0),FVector2D(1,0),FVector2D(-20,20),FVector2D(280,190));
        Guidance=Text(*WidgetTree,*Quest,TEXT(""),FLinearColor(1,.82,.45));
        auto* Hotbar=Panel(TEXT("WBP_QuickBar"),FVector2D(.5,1),FVector2D(.5,1),FVector2D(0,-20),FVector2D(540,160));
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Hotbar->AddChildToVerticalBox(Row);
        for(int32 I=0;I<4;++I)
        {
            auto* Col=WidgetTree->ConstructWidget<UVerticalBox>();Row->AddChildToHorizontalBox(Col)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* Icon=WidgetTree->ConstructWidget<UImage>();Icon->SetDesiredSizeOverride(FVector2D(28,28));Col->AddChildToVerticalBox(Icon);SkillIcons.Add(Icon);ShownIcons.Add(FString());
            Skills.Add(Text(*WidgetTree,*Col,FString::FromInt(I+1),FLinearColor(.9,.8,.5)));
            auto* Cooldown=WidgetTree->ConstructWidget<UProgressBar>();Cooldown->SetFillColorAndOpacity(FLinearColor(.4,.65,.95));Col->AddChildToVerticalBox(Cooldown);Cooldowns.Add(Cooldown);
        }
        State=Text(*WidgetTree,*Hotbar,TEXT(""));
        EquipmentText=Text(*WidgetTree,*Hotbar,TEXT(""));
        auto* Middle=Panel(TEXT("WBP_InteractionPrompt"),FVector2D(.5,.62),FVector2D(.5,.5),FVector2D::ZeroVector,FVector2D(520,130));
        PromptPanel=Middle->GetParent();
        auto* Target=Panel(TEXT("WBP_TargetVitals"),FVector2D(.5,0),FVector2D(.5,0),FVector2D(0,20),FVector2D(320,70));
        TargetName=Text(*WidgetTree,*Target,TEXT(""));TargetHealth=WidgetTree->ConstructWidget<UProgressBar>();Target->AddChildToVerticalBox(TargetHealth);TargetPanel=Target->GetParent();
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
    TArray<FAetherInspectStatusEffect> Effects;AetherNativeInventory::StatusEffects(*C,Effects);
    TArray<FGuid> EffectIds;for(const auto& E:Effects)EffectIds.Add(E.InstanceId);
    if(EffectIds!=ShownEffects)
    {
        ShownEffects=EffectIds;EffectsRow->ClearChildren();EffectLabels.Reset();
        for(const auto& E:Effects)
        {
            auto* Col=WidgetTree->ConstructWidget<UVerticalBox>();EffectsRow->AddChildToHorizontalBox(Col);
            auto* Icon=WidgetTree->ConstructWidget<UImage>();Icon->SetDesiredSizeOverride(FVector2D(24,24));Col->AddChildToVerticalBox(Icon);
            EffectLabels.Add(Text(*WidgetTree,*Col,E.DisplayName));
            const auto Path=AetherWidgetAssets::Icon(E.IconId);TWeakObjectPtr<UImage> WeakIcon=Icon;
            if(Path.IsValid())UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,[Path,WeakIcon]()
            {if(WeakIcon.IsValid())WeakIcon->SetBrushFromTexture(Cast<UTexture2D>(Path.ResolveObject()));});
        }
    }
    for(int32 I=0;I<Effects.Num();++I)
    {
        const auto& E=Effects[I];FString Label=E.DisplayName.Replace(TEXT(" · 旅舍祝福"),TEXT(""));
        if(E.ExpiresAtServerSeconds.IsSet())Label+=FString::Printf(TEXT("\n%.0f 秒"),FMath::Max(0.0,E.ExpiresAtServerSeconds.GetValue()-C->CombatTime()));
        EffectLabels[I]->SetText(FText::FromString(Label));
    }
    const auto* Net=LP->GetSubsystem<UAetherCommandClient>();const auto& Profile=Net->GetProfile();
    const auto& Definitions=FAetherV10Definitions::Get().Skills;
    for(int32 I=0;I<Skills.Num();++I)
    {
        const auto* Id=Profile.IsSet()?Profile->Skills.Hotbar.Find(I):nullptr;const auto* D=Id?Definitions.Skills.Find(*Id):nullptr;
        const FString IconId=D?D->IconId:FString();
        if(ShownIcons[I]!=IconId)
        {
            ShownIcons[I]=IconId;SkillIcons[I]->SetBrushFromTexture(nullptr);
            const auto Path=AetherWidgetAssets::Icon(IconId);const TWeakObjectPtr<UAetherPlayerHUDWidget> Self=this;
            if(Path.IsValid())UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,[Self,Path,I,IconId]()
            {if(Self.IsValid()&&Self->ShownIcons.IsValidIndex(I)&&Self->ShownIcons[I]==IconId)Self->SkillIcons[I]->SetBrushFromTexture(Cast<UTexture2D>(Path.ResolveObject()));});
        }
        const float Remaining=FMath::Max(0.f,C->CastLockUntil-C->CombatTime());
        Cooldowns[I]->SetPercent(D&&Remaining>0?FMath::Clamp(Remaining/FMath::Max(.01f,C->CastLockUntil-C->CastStartedAt),0.f,1.f):0);
        Skills[I]->SetText(FText::FromString(FString::Printf(TEXT("%s%d %s%s"),C->SelectedSpell==I?TEXT("▸ "):TEXT(""),I+1,D?*D->DisplayName:TEXT("未绑定"),D&&!C->SkillUnlocked(*Id)?TEXT(" · 未授权"):TEXT(""))));
    }
    FString Gear;
    if(Profile.IsSet())for(const TCHAR* Slot:{TEXT("MainHand"),TEXT("OffHand")})
    {
        const auto* Bound=Profile->Inventory.Equipment.Find(Slot);
        const auto* Item=Bound?Profile->Inventory.Find(*Bound):nullptr;
        const auto* D=Item?FAetherV10Definitions::Get().Items.Items.Find(Item->DefinitionId):nullptr;
        Gear+=(D?D->DisplayName:TEXT("空手"))+TEXT("  ");
    }
    const float Remaining=FMath::Max(0.f,C->CastLockUntil-C->CombatTime());
    if(Remaining>0)Gear+=FString::Printf(TEXT("施法恢复 %.1f 秒"),Remaining);
    EquipmentText->SetText(FText::FromString(Gear));
    auto* Target=C->LockedTarget.Get();
    TargetPanel->SetVisibility(Target&&Target->Alive()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(Target&&Target->Alive()){TargetName->SetText(FText::FromString(Target->Fighter==EAetherFighter::Player?TEXT("锁定角色"):TEXT("锁定敌人")));TargetHealth->SetPercent(Target->Health()/FMath::Max(1.f,Target->MaxHealth));}
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
    PromptPanel->SetVisibility(Interaction->GetText().IsEmpty()&&Feedback->GetText().IsEmpty()&&Activity.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}
