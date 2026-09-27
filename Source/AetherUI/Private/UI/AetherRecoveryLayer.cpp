#include "UI/AetherRecoveryLayer.h"
#include "UI/AetherPageWidgets.h"
#include "UI/AetherInputHints.h"
#include "Characters/AetherFrontierCharacter.h"
#include "CommonInputBaseTypes.h"
#include "Components/SizeBox.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
using namespace AetherPageWidgets;
UAetherRecoveryLayer::UAetherRecoveryLayer()
{
    bIsModal=true;
    // Esc/B are consumed in NativeOnPreviewKeyDown; no default CommonUI back action is configured.
    bIsBackHandler=false;SetIsFocusable(true);
}
TSharedRef<SWidget> UAetherRecoveryLayer::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(!WidgetTree->RootWidget)
    {
        auto* Shield=WidgetTree->ConstructWidget<UBorder>();Shield->SetBrushColor(FLinearColor(0,0,0,.78));
        Shield->SetHorizontalAlignment(HAlign_Center);Shield->SetVerticalAlignment(VAlign_Center);WidgetTree->RootWidget=Shield;
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(520);Shield->SetContent(Size);
        auto* Rows=WidgetTree->ConstructWidget<UVerticalBox>();Size->SetContent(Rows);
        Text(*WidgetTree,*Rows,TEXT("角色倒地"));
        Text(*WidgetTree,*Rows,TEXT("队友可在附近救援。回据点将结束本次倒地并重新同步角色。"));
        Status=Text(*WidgetTree,*Rows,TEXT("等待服务器同步"));
        WaitButton=Button(*WidgetTree,*Rows,TEXT("等待救援"),FSimpleDelegate::CreateWeakLambda(this,[this]{WaitButton->SetUserFocus(GetOwningPlayer());}));
        RecoverButton=Button(*WidgetTree,*Rows,TEXT("回据点（手柄 Y）"),FSimpleDelegate::CreateUObject(this,&UAetherRecoveryLayer::RequestRecovery),false);
        Feedback=Text(*WidgetTree,*Rows,TEXT(""));
    }
    return Super::RebuildWidget();
}
void UAetherRecoveryLayer::Refresh(AAetherFrontierCharacter* Pawn)
{
    if(BoundPawn.Get()!=Pawn||Life!=(Pawn?Pawn->RecoveryLife:FGuid()))
    {BoundPawn=Pawn;Life=Pawn?Pawn->RecoveryLife:FGuid();}
    if(!Status||!RecoverButton)return;
    const bool Valid=Pawn&&GetOwningPlayerPawn()==Pawn&&!Pawn->Alive()&&Life.IsValid();
    const bool Enabled=Valid&&Pawn->bRecoveryAvailable&&!Pawn->bRecoveryRequested;
    const bool LostFocus=!Enabled&&RecoverButton->HasUserFocus(GetOwningPlayer());
    RecoverButton->SetIsEnabled(Enabled);
    if(Pawn)if(auto* Label=Cast<UTextBlock>(RecoverButton->GetContent()))Label->SetText(FText::FromString(TEXT("回据点（")+AetherInputHints::Label(*Pawn,GetOwningLocalPlayer(),"F8")+TEXT("）")));
    if(LostFocus)WaitButton->SetUserFocus(GetOwningPlayer());
    FString Message=TEXT("等待服务器同步");
    if(Valid)
    {
        Message=Pawn->bRecoveryRequested?TEXT("恢复确认中，请等待角色同步"):Enabled?TEXT("可以继续等待救援，或返回据点"):Pawn->RecoveryReason;
        if(Pawn->RecoveryWait>0)Message+=FString::Printf(TEXT("（%.1f 秒）"),Pawn->RecoveryWait);
    }
    Status->SetText(FText::FromString(Message));Feedback->SetText(FText::FromString(Pawn?Pawn->Feedback:FString()));
}
void UAetherRecoveryLayer::RequestRecovery()
{
    auto* Pawn=BoundPawn.Get();
    if(!Pawn||GetOwningPlayerPawn()!=Pawn||Life!=Pawn->RecoveryLife||Pawn->Alive())return;
    Pawn->Recover();Refresh(Pawn);
}
FReply UAetherRecoveryLayer::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    auto* Pawn=BoundPawn.Get();
    if(E.GetKey()==EKeys::Gamepad_FaceButton_Top||(Pawn&&E.GetKey()==Pawn->BindingFor("F8")))
    {if(!E.IsRepeat())RequestRecovery();return FReply::Handled();}
    if(E.GetKey()==EKeys::Escape||E.GetKey()==EKeys::Gamepad_FaceButton_Right)return FReply::Handled();
    return Super::NativeOnPreviewKeyDown(G,E);
}
TOptional<FUIInputConfig> UAetherRecoveryLayer::GetDesiredInputConfig() const
{
    FUIInputConfig Config(ECommonInputMode::Menu,EMouseCaptureMode::NoCapture,EMouseLockMode::DoNotLock,false);
    Config.bIgnoreMoveInput=true;Config.bIgnoreLookInput=true;return Config;
}
UWidget* UAetherRecoveryLayer::NativeGetDesiredFocusTarget() const{return WaitButton;}
