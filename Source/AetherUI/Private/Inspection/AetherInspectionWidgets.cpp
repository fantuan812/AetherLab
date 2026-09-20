#include "Inspection/AetherInspectionWidgets.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/SpinBox.h"
#include "Engine/Texture2D.h"

void UAetherInspectionActionButton::InitializeAction(const FAetherInspectRequest& InRequest,const FAetherInspectionAction& InAction,bool Comparison)
{
    Request=InRequest;Action=InAction;bComparison=Comparison;SetIsEnabled(Action.bEnabled);
    SetToolTipText(FText::FromString(Action.DisabledReason));
    OnClicked.RemoveDynamic(this,&UAetherInspectionActionButton::Dispatch);
    OnClicked.AddDynamic(this,&UAetherInspectionActionButton::Dispatch);
}
void UAetherInspectionActionButton::Dispatch()
{if(Action.bEnabled&&GetIsEnabled())OnRequested.Broadcast(Request,Action,bComparison);}

TSharedRef<SWidget> UAetherInspectionCard::RebuildWidget()
{
    SetIsFocusable(true);
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
    if(!WidgetTree->RootWidget)
    {
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(420);Size->SetMaxDesiredHeight(620);
        WidgetTree->RootWidget=Size;
        auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetPadding(FMargin(16));Border->SetBrushColor(FLinearColor(.025f,.035f,.05f,.98f));Size->SetContent(Border);
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Root);
        auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Header);
        Icon=WidgetTree->ConstructWidget<UImage>();Icon->SetDesiredSizeOverride(FVector2D(48,48));Icon->SetVisibility(ESlateVisibility::Hidden);Header->AddChildToHorizontalBox(Icon);
        Heading=WidgetTree->ConstructWidget<UTextBlock>();Heading->SetAutoWrapText(true);
        auto* TitleSlot=Header->AddChildToHorizontalBox(Heading);TitleSlot->SetPadding(FMargin(12,0));TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();
        auto* ScrollSlot=Root->AddChildToVerticalBox(Scroll);ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));ScrollSlot->SetPadding(FMargin(0,12,0,0));
        Rows=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Rows);
    }
    RenderModel();return Super::RebuildWidget();
}
void UAetherInspectionCard::SetModel(const FAetherInspectionModel& InModel)
{
    ++Generation;Model=InModel;
    if(Icon){Icon->SetBrushFromTexture(nullptr);Icon->SetVisibility(ESlateVisibility::Hidden);}
    RenderModel();
}
void UAetherInspectionCard::SetResolvedIcon(uint64 DisplayGeneration,UTexture2D* Texture)
{
    if(DisplayGeneration!=Generation||!Icon)return;
    Icon->SetBrushFromTexture(Texture);Icon->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
}
void UAetherInspectionCard::AddLine(const FString& Text,FLinearColor Color)
{
    if(Text.IsEmpty()||!Rows)return;
    auto* Line=WidgetTree->ConstructWidget<UTextBlock>();Line->SetText(FText::FromString(Text));Line->SetAutoWrapText(true);
    Line->SetColorAndOpacity(FSlateColor(Color));Rows->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,3));
}
void UAetherInspectionCard::AddEffect(const TCHAR* Label,const FAetherSkillRankEffect& E)
{
    AddLine(Label,FLinearColor(.6f,.8f,1));
    AddLine(FString::Printf(TEXT("技能点 %d · 需求等级 %d"),E.PointCost,E.RequiredLevel));
    AddLine(FString::Printf(TEXT("法力 %.1f · 冷却 %.2f 秒 · 距离 %.0f 厘米 · 半径 %.0f 厘米"),E.ManaCost,E.Cooldown,E.RangeCm,E.TargetRadiusCm));
    // 直接呈现公共等级解析结果，不在 UI 重算材料反应或把水输出量当作免费资源。
    AddLine(FString::Printf(TEXT("热传递 %.1f J · 水转移 %.3f kg · 电刺激 %.1f J"),E.HeatJ,E.WaterKg,E.ElectricalJ));
}
void UAetherInspectionCard::RenderModel()
{
    if(!Rows||!Heading)return;
    Rows->ClearChildren();Heading->SetText(FText::FromString(Model.Title.IsEmpty()?TEXT("详细资料"):Model.Title));
    AddLine(Model.Message,Model.CanInteract()?FLinearColor(.8f,.85f,.9f):FLinearColor(1,.65f,.3f));
    for(const auto& Field:Model.Fields)AddLine(Field.Label+TEXT("：")+Field.Value);
    if(Model.CurrentSkillEffect.IsSet())AddEffect(TEXT("当前实际效果"),Model.CurrentSkillEffect.GetValue());
    if(Model.NextSkillEffect.IsSet())AddEffect(TEXT("下一级永久成长"),Model.NextSkillEffect.GetValue());
    if(Model.SelectedSkillEffect.IsSet())AddEffect(TEXT("所选节点效果"),Model.SelectedSkillEffect.GetValue());
    if(Model.RemainingSeconds.IsSet())AddLine(FString::Printf(TEXT("剩余 %.1f 秒"),Model.RemainingSeconds.GetValue()));
    AddLine(Model.ComparisonMessage);
    for(const auto& Stat:Model.Comparison)
    {
        const auto Color=Stat.Delta()>0?FLinearColor(.35f,1,.55f):Stat.Delta()<0?FLinearColor(1,.45f,.4f):FLinearColor(.8f,.8f,.8f);
        // 正负号、实际数值与颜色同时表达变化，不能只用红绿区分。
        AddLine(FString::Printf(TEXT("%s：%.2f → %.2f (%+.2f)"),*Stat.Id,Stat.Before,Stat.After,Stat.Delta()),Color);
    }
    if(!Model.DisplacedInstances.IsEmpty())AddLine(FString::Printf(TEXT("此方案将卸下 %d 件现有装备。"),Model.DisplacedInstances.Num()));
    auto AddButton=[&](const FAetherInspectionAction& Action,bool Comparison)
    {
        auto* B=WidgetTree->ConstructWidget<UAetherInspectionActionButton>();
        B->InitializeAction(Model.Request,Action,Comparison);
        B->OnRequested.AddUObject(this,&UAetherInspectionCard::Forward);
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetAutoWrapText(true);
        Text->SetText(FText::FromString(Action.Label+(Action.Argument.IsEmpty()?FString():TEXT(" · ")+Action.Argument)));
        B->SetContent(Text);Rows->AddChildToVerticalBox(B)->SetPadding(FMargin(0,4));
        if(!Action.bEnabled)AddLine(Action.DisabledReason,FLinearColor(.8f,.65f,.5f));
    };
    if(Model.CanInteract())
    {
        for(const auto& Slot:Model.ComparisonSlots)
            AddButton({EAetherInspectAction::Equip,Slot,TEXT("比较此槽"),{},1,true,false},true);
        for(const auto& Action:Model.Actions)AddButton(Action,false);
    }
}
void UAetherInspectionCard::Forward(const FAetherInspectRequest& Request,const FAetherInspectionAction& Action,bool Comparison)
{
    // 子按钮已经移除但点击还在队列中的情况也不能派发到新展示对象。
    if(!Model.CanInteract()||!Request.Context.Same(Model.Request.Context)||Request.Target.Kind!=Model.Request.Target.Kind||
        Request.Target.InstanceId!=Model.Request.Target.InstanceId||Request.Target.SkillRank!=Model.Request.Target.SkillRank||
        !Request.Target.DefinitionId.Equals(Model.Request.Target.DefinitionId,ESearchCase::CaseSensitive)||
        !Request.Target.SlotId.Equals(Model.Request.Target.SlotId,ESearchCase::CaseSensitive))return;
    if(Comparison)
    {
        if(Model.ComparisonSlots.Contains(Action.Argument))OnComparisonRequested.Broadcast(Request,Action.Argument);
    }
    else
    {
        const auto* Current=Model.Actions.FindByPredicate([&](const auto& A)
            {return A.Kind==Action.Kind&&A.Argument.Equals(Action.Argument,ESearchCase::CaseSensitive);});
        if(Current&&Current->bEnabled)OnActionRequested.Broadcast(Request,*Current);
    }
}
FReply UAetherInspectionCard::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    // 卡片内部的右键被 UI 消费；打开详情和右键本身都不执行装备、学习或角色格挡。
    if(Event.GetEffectingButton()==EKeys::RightMouseButton)return FReply::Handled();
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}
FReply UAetherInspectionCard::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::Escape||Event.GetKey()==EKeys::Gamepad_FaceButton_Right)
    {if(!Event.IsRepeat())OnDismissRequested.Broadcast();return FReply::Handled();}
    return Super::NativeOnKeyDown(Geometry,Event);
}
void UAetherInspectionCard::NativeDestruct()
{
    ++Generation;if(Icon)Icon->SetBrushFromTexture(nullptr);
    OnActionRequested.Clear();OnComparisonRequested.Clear();OnDismissRequested.Clear();Super::NativeDestruct();
}

TSharedRef<SWidget> UAetherInspectionConfirmation::RebuildWidget()
{
    SetIsFocusable(true);
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
    if(!WidgetTree->RootWidget)
    {
        auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetPadding(FMargin(20));Border->SetBrushColor(FLinearColor(.04f,.05f,.07f,1));WidgetTree->RootWidget=Border;
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Root);
        Prompt=WidgetTree->ConstructWidget<UTextBlock>();Prompt->SetAutoWrapText(true);Root->AddChildToVerticalBox(Prompt);
        Quantity=WidgetTree->ConstructWidget<USpinBox>();Quantity->SetDelta(1);Quantity->SetMinFractionalDigits(0);Quantity->SetMaxFractionalDigits(0);
        Root->AddChildToVerticalBox(Quantity)->SetPadding(FMargin(0,12));
        auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Actions);
        ConfirmButton=WidgetTree->ConstructWidget<UButton>();auto* ConfirmText=WidgetTree->ConstructWidget<UTextBlock>();
        ConfirmText->SetText(FText::FromString(TEXT("确认")));ConfirmButton->SetContent(ConfirmText);Actions->AddChildToHorizontalBox(ConfirmButton);
        ConfirmButton->OnClicked.AddDynamic(this,&UAetherInspectionConfirmation::Confirm);
        auto* CancelButton=WidgetTree->ConstructWidget<UButton>();auto* CancelText=WidgetTree->ConstructWidget<UTextBlock>();
        CancelText->SetText(FText::FromString(TEXT("取消")));CancelButton->SetContent(CancelText);Actions->AddChildToHorizontalBox(CancelButton);
        CancelButton->OnClicked.AddDynamic(this,&UAetherInspectionConfirmation::Cancel);
    }
    RefreshDraft();return Super::RebuildWidget();
}
void UAetherInspectionConfirmation::SetDraft(const FAetherInspectionDraft& InDraft)
{
    Draft=InDraft;RefreshDraft();if(Quantity)Quantity->SetValue(1);
}
void UAetherInspectionConfirmation::InvalidateDraft(){Draft.Reset();RefreshDraft();}
void UAetherInspectionConfirmation::RefreshDraft()
{
    if(!Prompt||!Quantity||!ConfirmButton)return;
    const bool Valid=Draft.IsSet()&&Draft->Token.IsValid()&&Draft->Action.bEnabled&&Draft->Action.MaxQuantity>=1&&Draft->Action.MaxQuantity<=1000;
    Prompt->SetText(FText::FromString(Valid?TEXT("确认")+Draft->Action.Label+TEXT("？"):TEXT("对象已变化，请重新查看。")));
    ConfirmButton->SetIsEnabled(Valid);Quantity->SetIsEnabled(Valid);
    Quantity->SetMinValue(1);Quantity->SetMinSliderValue(1);Quantity->SetMaxValue(Valid?Draft->Action.MaxQuantity:1);Quantity->SetMaxSliderValue(Valid?Draft->Action.MaxQuantity:1);
    Quantity->SetVisibility(Valid&&Draft->Action.MaxQuantity>1?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
}
void UAetherInspectionConfirmation::Confirm()
{
    if(!Draft.IsSet()||!Quantity||!ConfirmButton||!ConfirmButton->GetIsEnabled()||!FMath::IsFinite(Quantity->GetValue()))return;
    const int32 Count=FMath::Clamp(FMath::RoundToInt(Quantity->GetValue()),1,Draft->Action.MaxQuantity);
    const FGuid Token=Draft->Token;InvalidateDraft();OnConfirmed.Broadcast(Token,Count);
}
void UAetherInspectionConfirmation::Cancel()
{
    if(!Draft.IsSet())return;
    const FGuid Token=Draft->Token;InvalidateDraft();OnCancelled.Broadcast(Token);
}
FReply UAetherInspectionConfirmation::NativeOnMouseButtonDown(const FGeometry&,const FPointerEvent&)
{return FReply::Handled();}
FReply UAetherInspectionConfirmation::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::Escape||Event.GetKey()==EKeys::Gamepad_FaceButton_Right)
    {if(!Event.IsRepeat())Cancel();return FReply::Handled();}
    return Super::NativeOnKeyDown(Geometry,Event);
}
void UAetherInspectionConfirmation::NativeDestruct()
{InvalidateDraft();OnConfirmed.Clear();OnCancelled.Clear();Super::NativeDestruct();}
