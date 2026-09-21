#include "Settings/AetherSettingsPage.h"
#include "UI/AetherWidgetAssets.h"
#include "UI/AetherPageWidgets.h"
#include "Presentation/AetherPlayerPreferences.h"
#include "Input/AetherInputProfile.h"
#include "Characters/AetherFrontierCharacter.h"
#include "AetherMotionComponent.h"
#include "Components/SpinBox.h"
#include "Components/CheckBox.h"
#include "Components/InputKeySelector.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Engine/Engine.h"
#include "Engine/UserInterfaceSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "AudioDevice.h"
#include "HAL/IConsoleManager.h"
using namespace AetherPageWidgets;
TSharedRef<SWidget> UAetherSettingsPage::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(WidgetTree->RootWidget)AetherWidgetAssets::BindDesigner(*this,*WidgetTree);
    if(!WidgetTree->RootWidget)
    {
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();WidgetTree->RootWidget=Scroll;
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Root);
        auto Combo=[&](const TCHAR* Name,const TArray<FString>& Options){Text(*WidgetTree,*Root,Name);auto* C=WidgetTree->ConstructWidget<UComboBoxString>();for(const FString& O:Options)C->AddOption(O);Root->AddChildToVerticalBox(C);return C;};
        Resolution=Combo(TEXT("分辨率"),{TEXT("1280x720"),TEXT("1920x1080"),TEXT("2560x1440"),TEXT("3440x1440"),TEXT("3840x2160")});
        WindowMode=Combo(TEXT("窗口模式"),{TEXT("独占全屏"),TEXT("无边框全屏"),TEXT("窗口")});
        Quality=Combo(TEXT("图形质量"),{TEXT("低"),TEXT("中"),TEXT("高"),TEXT("极高"),TEXT("影视")});
        Vsync=WidgetTree->ConstructWidget<UCheckBox>();Root->AddChildToVerticalBox(Vsync);auto* VText=WidgetTree->ConstructWidget<UTextBlock>();VText->SetText(FText::FromString(TEXT("垂直同步")));Vsync->SetContent(VText);
        auto Number=[&](const TCHAR* Name,float Min,float Max,float Step){Text(*WidgetTree,*Root,Name);auto* S=WidgetTree->ConstructWidget<USpinBox>();S->SetMinValue(Min);S->SetMaxValue(Max);S->SetDelta(Step);Root->AddChildToVerticalBox(S);return S;};
        Volume=Number(TEXT("主音量"),0,1,.05);
        Mouse=Number(TEXT("鼠标视角灵敏度"),.1,3,.1);
        Controller=Number(TEXT("手柄视角灵敏度"),.1,3,.1);
        Scale=Number(TEXT("界面缩放"),.75,1.5,.05);
        Invert=WidgetTree->ConstructWidget<UCheckBox>();Root->AddChildToVerticalBox(Invert);auto* IText=WidgetTree->ConstructWidget<UTextBlock>();IText->SetText(FText::FromString(TEXT("反转垂直视角")));Invert->SetContent(IText);
        Backend=Combo(TEXT("角色动作"),{TEXT("自动选择"),TEXT("传统动画"),TEXT("CPU 生成"),TEXT("Vulkan 生成")});
        Provider=Text(*WidgetTree,*Root,TEXT(""));
        BindingAction=Combo(TEXT("按键 · 相同输入场景内的冲突会交换"),{});
        BindingKey=WidgetTree->ConstructWidget<UInputKeySelector>();BindingKey->SetAllowModifierKeys(false);BindingKey->SetAllowGamepadKeys(false);Root->AddChildToVerticalBox(BindingKey);
        BindingAction->OnSelectionChanged.AddDynamic(this,&UAetherSettingsPage::ActionSelected);BindingKey->OnKeySelected.AddDynamic(this,&UAetherSettingsPage::KeySelected);
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Row);
        auto Action=[&](const TCHAR* Label,FSimpleDelegate Fn){auto* Col=WidgetTree->ConstructWidget<UVerticalBox>();Row->AddChildToHorizontalBox(Col);Button(*WidgetTree,*Col,Label,MoveTemp(Fn));};
        Action(TEXT("保存并应用"),FSimpleDelegate::CreateUObject(this,&UAetherSettingsPage::Apply));
        Action(TEXT("取消更改"),FSimpleDelegate::CreateWeakLambda(this,[this](){RevertVideo();RefreshPage();}));
        Action(TEXT("恢复默认"),FSimpleDelegate::CreateUObject(this,&UAetherSettingsPage::Defaults));
        Confirmation=WidgetTree->ConstructWidget<UVerticalBox>();Root->AddChildToVerticalBox(Confirmation);
        Button(*WidgetTree,*Confirmation,TEXT("保留此分辨率"),FSimpleDelegate::CreateUObject(this,&UAetherSettingsPage::ConfirmVideo));
        Button(*WidgetTree,*Confirmation,TEXT("恢复原分辨率"),FSimpleDelegate::CreateUObject(this,&UAetherSettingsPage::RevertVideo));
        Notice=Text(*WidgetTree,*Root,TEXT(""));Confirmation->SetVisibility(ESlateVisibility::Collapsed);
    }

    if(BindingAction)BindingAction->OnSelectionChanged.AddUniqueDynamic(this,&UAetherSettingsPage::ActionSelected);
    if(BindingKey)BindingKey->OnKeySelected.AddUniqueDynamic(this,&UAetherSettingsPage::KeySelected);
    const auto Bind=[&](const TCHAR* Name,FSimpleDelegate Action){if(auto* B=Cast<UAetherPageButton>(GetWidgetFromName(Name)))B->Bind(MoveTemp(Action));};
    Bind(TEXT("ApplyButton"),FSimpleDelegate::CreateUObject(this,&UAetherSettingsPage::Apply));
    Bind(TEXT("RevertButton"),FSimpleDelegate::CreateWeakLambda(this,[this](){RevertVideo();RefreshPage();}));
    Bind(TEXT("DefaultsButton"),FSimpleDelegate::CreateUObject(this,&UAetherSettingsPage::Defaults));
    Bind(TEXT("ConfirmVideoButton"),FSimpleDelegate::CreateUObject(this,&UAetherSettingsPage::ConfirmVideo));
    Bind(TEXT("RevertVideoButton"),FSimpleDelegate::CreateUObject(this,&UAetherSettingsPage::RevertVideo));
    return Super::RebuildWidget();
}
UWidget* UAetherSettingsPage::InitialFocus() const{return Resolution?Resolution.Get():Super::InitialFocus();}
bool UAetherSettingsPage::IsSelectingKey() const{return BindingKey&&BindingKey->GetIsSelectingKey();}
void UAetherSettingsPage::RefreshPage()
{
    if(!Resolution||bVideoPending)return;bLoading=true;
    const auto* S=GEngine->GetGameUserSettings();const auto* P=GetDefault<UAetherPlayerPreferences>();
    const FString Value=FString::Printf(TEXT("%dx%d"),S->GetScreenResolution().X,S->GetScreenResolution().Y);
    if(Resolution->FindOptionIndex(Value)==INDEX_NONE)Resolution->AddOption(Value);Resolution->SetSelectedOption(Value);
    WindowMode->SetSelectedIndex(int32(S->GetFullscreenMode()));Quality->SetSelectedIndex(FMath::Clamp(S->GetOverallScalabilityLevel(),0,4));Vsync->SetIsChecked(S->IsVSyncEnabled());
    Volume->SetValue(P->MasterVolume);Mouse->SetValue(P->MouseSensitivity);Controller->SetValue(P->ControllerSensitivity);Scale->SetValue(P->UIScale);Invert->SetIsChecked(P->bInvertLook);
    Backend->SetSelectedIndex(FMath::Clamp(P->MotionBackend+1,0,3));
    DraftKeys.Reset();BindingAction->ClearOptions();
    if(auto* C=Player())
    {
        TArray<FName> Names;C->InputDefaults().GetKeys(Names);Names.Sort(FNameLexicalLess());
        for(FName Name:Names)if(Name!="LookX"&&Name!="LookY"&&Name!="Escape"&&!Name.ToString().StartsWith(TEXT("Pad")))
        {DraftKeys.Add(Name,C->BindingFor(Name));BindingAction->AddOption(Name.ToString());}
    }
    if(BindingAction->GetOptionCount())BindingAction->SetSelectedIndex(0);bLoading=false;ActionSelected(BindingAction->GetSelectedOption(),ESelectInfo::Direct);
}
void UAetherSettingsPage::ActionSelected(FString Value,ESelectInfo::Type)
{
    if(bLoading||!BindingKey)return;TGuardValue<bool> Guard(bLoading,true);BindingKey->SetSelectedKey(FInputChord(DraftKeys.FindRef(FName(Value))));
}
void UAetherSettingsPage::KeySelected(FInputChord Chord)
{
    if(bLoading||!Chord.Key.IsValid()||Chord.Key.IsGamepadKey()||Chord.Key==EKeys::Escape||Chord.Key.IsAxis1D()||Chord.Key.IsAxis2D())return;
    const FName Name(BindingAction->GetSelectedOption());if(!DraftKeys.Contains(Name))return;const FKey Old=DraftKeys[Name];
    for(auto& Pair:DraftKeys)if(Pair.Key!=Name&&Pair.Value==Chord.Key&&(UAetherInputProfile::Context(Name)&UAetherInputProfile::Context(Pair.Key)))Pair.Value=Old;
    DraftKeys[Name]=Chord.Key;Notice->SetText(FText::FromString(TEXT("键位变更尚未保存；同一场景内冲突已交换。")));
}
void UAetherSettingsPage::Defaults()
{
    RevertVideo();Volume->SetValue(.8);Mouse->SetValue(1);Controller->SetValue(1);Scale->SetValue(1);Invert->SetIsChecked(false);Backend->SetSelectedIndex(0);Quality->SetSelectedIndex(2);Vsync->SetIsChecked(false);
    WindowMode->SetSelectedIndex(1);Resolution->SetSelectedOption(TEXT("1920x1080"));
    if(auto* C=Player())for(auto& Pair:DraftKeys)Pair.Value=C->InputDefaults().FindRef(Pair.Key);
    ActionSelected(BindingAction->GetSelectedOption(),ESelectInfo::Direct);Notice->SetText(FText::FromString(TEXT("默认值已填入，保存后生效。")));
}
void UAetherSettingsPage::Apply()
{
    if(bVideoPending)return;auto* S=GEngine->GetGameUserSettings();FString X,Y;
    if(!Resolution->GetSelectedOption().Split(TEXT("x"),&X,&Y))return;const FIntPoint Size(FCString::Atoi(*X),FCString::Atoi(*Y));
    if(Size.X<640||Size.X>7680||Size.Y<480||Size.Y>4320)return;
    BeforeResolution=S->GetScreenResolution();BeforeWindowMode=int32(S->GetFullscreenMode());BeforeQuality=S->GetOverallScalabilityLevel();BeforeVsync=S->IsVSyncEnabled();
    S->SetScreenResolution(Size);S->SetFullscreenMode(EWindowMode::Type(WindowMode->GetSelectedIndex()));
    S->SetOverallScalabilityLevel(Quality->GetSelectedIndex());S->SetVSyncEnabled(Vsync->IsChecked());S->ApplySettings(false);
    if(Size!=BeforeResolution||BeforeWindowMode!=WindowMode->GetSelectedIndex())
    {
        bVideoPending=true;ConfirmDeadline=FPlatformTime::Seconds()+15;
        Confirmation->SetVisibility(ESlateVisibility::Visible);Notice->SetText(FText::FromString(TEXT("十五秒内确认可见的新分辨率，否则自动恢复。")));
    }
    else SaveDraft();
}
void UAetherSettingsPage::SaveDraft()
{
    auto* P=GetMutableDefault<UAetherPlayerPreferences>();
    P->MasterVolume=FMath::Clamp(Volume->GetValue(),0.f,1.f);P->MouseSensitivity=FMath::Clamp(Mouse->GetValue(),.1f,3.f);
    P->ControllerSensitivity=FMath::Clamp(Controller->GetValue(),.1f,3.f);P->UIScale=FMath::Clamp(Scale->GetValue(),.75f,1.5f);
    P->bInvertLook=Invert->IsChecked();P->MotionBackend=Backend->GetSelectedIndex()-1;P->SaveConfig();
    if(auto* C=Player())for(const auto& Pair:DraftKeys)C->AetherBind(Pair.Key,Pair.Value);
    GEngine->GetGameUserSettings()->SaveSettings();ApplyLocalPreferences(GetWorld());Notice->SetText(FText::FromString(TEXT("设置已保存。")));
}
void UAetherSettingsPage::ConfirmVideo()
{
    if(!bVideoPending)return;bVideoPending=false;GEngine->GetGameUserSettings()->ConfirmVideoMode();
    Confirmation->SetVisibility(ESlateVisibility::Collapsed);SaveDraft();
}
void UAetherSettingsPage::RevertVideo()
{
    if(!bVideoPending)return;bVideoPending=false;auto* S=GEngine->GetGameUserSettings();
    S->SetScreenResolution(BeforeResolution);S->SetFullscreenMode(EWindowMode::Type(BeforeWindowMode));S->SetOverallScalabilityLevel(BeforeQuality);S->SetVSyncEnabled(BeforeVsync);
    S->ApplySettings(false);S->ConfirmVideoMode();S->SaveSettings();if(Confirmation)Confirmation->SetVisibility(ESlateVisibility::Collapsed);
    if(Notice)Notice->SetText(FText::FromString(TEXT("显示设置已恢复；其他修改仍未保存。")));
}
void UAetherSettingsPage::PageClosed(){RevertVideo();}
void UAetherSettingsPage::LiveRefresh()
{
    if(bVideoPending){const int32 Left=FMath::CeilToInt(ConfirmDeadline-FPlatformTime::Seconds());if(Left<=0)RevertVideo();else Notice->SetText(FText::FromString(FString::Printf(TEXT("剩余 %d 秒确认，否则恢复原分辨率。"),Left)));}
    if(auto* C=Player();C&&C->Motion&&Provider)Provider->SetText(FText::FromString(TEXT("当前动作状态：")+C->Motion->Status()));
}
void UAetherSettingsPage::ApplyLocalPreferences(UWorld* World)
{
    const auto* P=GetDefault<UAetherPlayerPreferences>();
    GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale=FMath::Clamp(P->UIScale,.75f,1.5f);
    if(World)if(auto Device=World->GetAudioDevice();Device.IsValid())Device->SetTransientPrimaryVolume(FMath::Clamp(P->MasterVolume,0.f,1.f));
    if(auto* CVar=IConsoleManager::Get().FindConsoleVariable(TEXT("aether.Motion.Backend")))CVar->Set(FMath::Clamp(P->MotionBackend,-1,2),ECVF_SetByGameSetting);
}
