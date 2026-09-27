#include "Tests/AetherMenuInteractionProbe.h"
#include "UI/AetherFrontierHUD.h"
#include "AetherFrontierPanel.h"
#include "Framework/AetherFrontier.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "InputKeyEventArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Networking/AetherCommandClient.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Preview/AetherCharacterPreviewSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "UI/AetherMenuRoot.h"
#include "UI/AetherRecoveryLayer.h"
#include "Inventory/AetherInventoryPage.h"
#include "Inventory/AetherInventoryCell.h"
#include "Components/EditableTextBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/WidgetSwitcher.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Widgets/Input/SEditableTextBox.h"

#if !UE_BUILD_SHIPPING
namespace
{
// Controlled fixtures establish downed state; UI recovery still uses real Slate input and the production service.
void TickClosureLight(AAetherFrontierHUD* HUD,UAetherFrontierPanel* Panel)
{
 static int32 Step=0;static float Next=0,BeforeStamina=0;static bool SearchSet=false;
 static TWeakObjectPtr<AAetherFrontierCharacter> DownedPawn;
 static TWeakObjectPtr<UAetherRecoveryLayer> OldLayer;
 auto* PC=HUD->GetOwningPlayerController();auto* C=Cast<AAetherFrontierCharacter>(PC->GetPawn());
 auto* Menu=PC->GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>();
 const float Now=HUD->GetWorld()->GetTimeSeconds();if(Now<Next)return;
 const auto Check=[&](bool Value,const TCHAR* Name)
 {
  UE_LOG(LogTemp,Display,TEXT("CLOSURE_LIGHT_%s step=%d %s"),Value?TEXT("PASS"):TEXT("FAIL"),Step,Name);
  if(!Value){UE_LOG(LogTemp,Error,TEXT("V10_MENU_INTERACTION_FAIL"));FPlatformMisc::RequestExitWithStatus(false,1);}
  return Value;
 };
 const auto Key=[&](FKey K,bool Press){PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,Press?IE_Pressed:IE_Released,Press?1.f:0.f));};
 const auto Recovery=[&]()->UAetherRecoveryLayer*
 {auto* Stack=Cast<UCommonActivatableWidgetStack>(HUD->MenuRoot->GetWidgetFromName(TEXT("MainStack")));return Stack?Cast<UAetherRecoveryLayer>(Stack->GetActiveWidget()):nullptr;};
 const auto UIKey=[&](UAetherRecoveryLayer* Layer,FKey K)
 {Layer->SetKeyboardFocus();return FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));};
 switch(Step)
 {
 case 0:
  if(!C||!C->Ready()||Menu->IsOpen())return;
  C->SelectedSpell=2;BeforeStamina=C->Stamina();Key(EKeys::Gamepad_LeftShoulder,true);break;
 case 1:
  if(!Check(C&&!C->IsCrouched()&&!C->bCrouchToggled,TEXT("LB modifier has no crouch side effect")))return;
  Key(EKeys::Gamepad_DPad_Up,true);break;
 case 2:
  if(!Check(C->SelectedSpell==2&&!C->IsCrouched(),TEXT("LB+DPad does not select base skill or crouch")))return;
  Key(EKeys::Gamepad_DPad_Up,false);Key(EKeys::Gamepad_FaceButton_Right,true);break;
 case 3:
  if(!Check(C->IsCrouched()&&C->bCrouchToggled&&C->Stamina()>=BeforeStamina-.1f,TEXT("LB+B crouches without dodge cost")))return;
  Key(EKeys::Gamepad_FaceButton_Right,false);break;
 case 4: Key(EKeys::Gamepad_FaceButton_Right,true);break;
 case 5:
  if(!Check(!C->IsCrouched()&&!C->bCrouchToggled,TEXT("Second LB+B safely stands")))return;
  Key(EKeys::Gamepad_FaceButton_Right,false);Key(EKeys::Gamepad_LeftShoulder,false);break;
 case 6:
  if(!C->Ready())return;BeforeStamina=C->Stamina();Key(EKeys::Gamepad_FaceButton_Right,true);break;
 case 7:
  if(!Check(C->Stamina()<BeforeStamina-1&&!C->bCrouchToggled,TEXT("B without LB remains dodge")))return;
  Key(EKeys::Gamepad_FaceButton_Right,false);break;
 case 8:
  if(!C->Ready())return;Menu->OpenPage(EAetherMenuPage::Inventory);break;
 case 9:
 {
  auto* Host=Cast<UWidgetSwitcher>(Panel->GetWidgetFromName(TEXT("PageHost")));
  auto* Page=Host?Cast<UAetherInventoryPage>(Host->GetActiveWidget()):nullptr;
  auto* Search=Page?Cast<UEditableTextBox>(Page->GetWidgetFromName(TEXT("Search"))):nullptr;
  auto* Grid=Page?Cast<UUniformGridPanel>(Page->GetWidgetFromName(TEXT("Grid"))):nullptr;
  if(!Check(Search&&Grid&&Grid->GetChildrenCount()>0,TEXT("Production inventory search and grid available")))return;
  if(!SearchSet)
  {
   Grid->GetChildAt(0)->SetUserFocus(PC);
   if(!Check(Grid->GetChildAt(0)->HasUserFocus(PC),TEXT("Inventory cell owns focus before filtering")))return;
   // UEditableTextBox::SetText in UE 5.8 intentionally does not emit OnTextChanged.
   // Change the Slate text to exercise the real UMG text-change handler while a cell owns focus.
   StaticCastSharedRef<SEditableTextBox>(Search->TakeWidget())->SetText(FText::FromString(TEXT("__closure_no_match__")));
   SearchSet=true;Next=Now+.35f;return;
  }
  bool AllFiltered=true;for(auto* Child:Grid->GetAllChildren()){const auto* Cell=Cast<UAetherInventoryCell>(Child);AllFiltered&=Cell&&Cell->IsFiltered();}
  UE_LOG(LogTemp,Display,TEXT("CLOSURE_LIGHT_SEARCH filtered=%d outerFocus=%d innerFocus=%d cellFocus=%d"),AllFiltered,Search->HasUserFocus(PC),Search->HasUserFocusedDescendants(PC),Grid->GetChildAt(0)->HasUserFocus(PC));
  // SEditableTextBox forwards user focus to its inner SEditableText.
  if(!Check(AllFiltered&&(Search->HasUserFocus(PC)||Search->HasUserFocusedDescendants(PC)),TEXT("Filtering every slot restores search focus")))return;
  StaticCastSharedRef<SEditableTextBox>(Search->TakeWidget())->SetText(FText::GetEmpty());Menu->Close();break;
 }
 case 10:
  if(!C->Ready())return;DownedPawn=C;C->CombatRuntime->LastDamageAt=C->CombatTime();C->SetVitals(0,C->Mana(),C->Stamina());break;
 case 11:
 {
  auto* Layer=Recovery();
  if(!Check(C==DownedPawn.Get()&&!C->Alive()&&Layer&&Layer->IsActivated()&&Menu->GetPage()==EAetherMenuPage::Recovery&&!C->bRecoveryAvailable,TEXT("Downed layer owns focus during server wait")))return;
  FString Dir;FParse::Value(FCommandLine::Get(),TEXT("AetherMenuCaptureDir="),Dir);
  FScreenshotRequest::RequestScreenshot(Dir/TEXT("ClosureDowned.png"),true,false);
  OldLayer=Layer;UIKey(Layer,EKeys::Gamepad_FaceButton_Top);break;
 }
 case 12:
  if(!Check(C==DownedPawn.Get()&&!C->bRecoveryRequested,TEXT("Early recovery input cannot bypass server wait")))return;
  if(!Check(Recovery()&&UIKey(Recovery(),EKeys::Escape)&&UIKey(Recovery(),EKeys::Gamepad_FaceButton_Right)&&Menu->GetPage()==EAetherMenuPage::Recovery&&Recovery()->IsActivated(),TEXT("Escape and gamepad B cannot dismiss downed layer")))return;
  break;
 case 13:
  if(!C->bRecoveryAvailable)return;
  if(!Check(Recovery()&&UIKey(Recovery(),EKeys::Gamepad_FaceButton_Top),TEXT("Gamepad Y reaches recovery through Slate")))return;
  if(OldLayer.IsValid())OldLayer->NativeOnPreviewKeyDown(FGeometry(),FKeyEvent(EKeys::Gamepad_FaceButton_Top,FModifierKeysState(),0,false,0,0));
  break;
 case 14:
  if(!C||C==DownedPawn.Get()||!C->Ready())return;
  if(!Check(C->Alive()&&!Menu->IsOpen()&&!PC->bShowMouseCursor&&!C->bRecoveryRequested,TEXT("Recovery replaces pawn and restores game input exactly once")))return;
  if(OldLayer.IsValid())OldLayer->NativeOnPreviewKeyDown(FGeometry(),FKeyEvent(EKeys::Gamepad_FaceButton_Top,FModifierKeysState(),0,false,0,0));
  DownedPawn=C;break;
 case 15:
  if(!Check(C==DownedPawn.Get()&&C->Alive(),TEXT("Late old-layer confirmation cannot affect new pawn")))return;
  C->CombatRuntime->LastDamageAt=C->CombatTime();C->SetVitals(0,C->Mana(),C->Stamina());break;
 case 16:
  if(!Check(Recovery()&&Menu->IsOpen(),TEXT("Second downed life opens a fresh layer")))return;
  OldLayer=Recovery();C->SetVitals(50,C->Mana(),C->Stamina());break;
 case 17:
  if(!Check(C==DownedPawn.Get()&&C->Alive()&&!Menu->IsOpen(),TEXT("Rescue state closes layer without respawning")))return;
  if(OldLayer.IsValid())OldLayer->NativeOnPreviewKeyDown(FGeometry(),FKeyEvent(EKeys::Gamepad_FaceButton_Top,FModifierKeysState(),0,false,0,0));break;
 default:
  if(!Check(C==DownedPawn.Get()&&C->Alive()&&!Menu->IsOpen(),TEXT("Stale rescue confirmation is ignored")))return;
  UE_LOG(LogTemp,Display,TEXT("V10_MENU_INTERACTION_PASS closure=1 cycles=0 native=1"));FPlatformMisc::RequestExit(false);return;
 }
 ++Step;Next=Now+.35f;
}
}
#endif

void AetherMenuInteraction::Tick(AAetherFrontierHUD* HUD,UAetherFrontierPanel* Panel)
{
#if !UE_BUILD_SHIPPING
 static const bool Enabled=FParse::Param(FCommandLine::Get(),TEXT("AetherV10MenuCapture"));
 if(!Enabled||!HUD||!Panel)return;
 // 仅该短启动进程使用一个本地玩家；不注册到正式输入路径，也不接触个人存档。
 static int32 Stage=0;static float NextAt=3;static FKey PendingRelease;
 static uint64 IdleRefreshes=0;static TWeakObjectPtr<AAetherFrontierCharacter> OldPawn;
 static TWeakObjectPtr<AAetherPlayerState> OldProfile;static FVector SpawnLocation;
 static bool Failed=false;
 if(Failed)return;
 auto Check=[&](bool Passed,const TCHAR* Label)
 {
  UE_LOG(LogTemp,Display,TEXT("V10_MENU_CHECK %s stage=%d %s"),Passed?TEXT("PASS"):TEXT("FAIL"),Stage,Label);
  if(!Passed){Failed=true;UE_LOG(LogTemp,Error,TEXT("V10_MENU_INTERACTION_FAIL"));FPlatformMisc::RequestExitWithStatus(false,1);}
  return Passed;
 };
 auto* PC=HUD->GetOwningPlayerController();if(!PC)return;
 auto* C=Cast<AAetherFrontierCharacter>(PC->GetPawn());
 auto* LP=PC->GetLocalPlayer();if(!LP)return;
 auto* Client=LP->GetSubsystem<UAetherCommandClient>();auto* Menu=LP->GetSubsystem<UAetherMenuSubsystem>();
 const float Now=HUD->GetWorld()->GetTimeSeconds();
 if(PendingRelease.IsValid()){PC->InputKey(FInputKeyEventArgs::CreateSimulated(PendingRelease,IE_Released,0.f));PendingRelease=FKey();}
 if(Now<NextAt)return;
 if(Now>70){Check(false,TEXT("deadline"));return;}
 auto GameKey=[&](FKey Key){PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Pressed,1.f));PendingRelease=Key;};
 auto FocusedKey=[&](FKey Key)
 {
  auto* Target=Panel->GetPrimaryFocusTarget();if(!Target)return false;
  Target->SetKeyboardFocus();
  // 经 Slate 的真实焦点路径进入 NativeOnPreviewKeyDown，覆盖按钮吃掉菜单快捷键的回归。
  return FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key,FModifierKeysState(),0,false,0,0));
 };
 auto Capture=[&](const TCHAR* Name)
 {
  FString Dir;FParse::Value(FCommandLine::Get(),TEXT("AetherMenuCaptureDir="),Dir);
  FScreenshotRequest::RequestScreenshot(Dir/Name,true,false);
 };
 switch(Stage)
 {
 case 0:
  if(!C||!C->ProfileState()||!C->Ready()||!C->HasGameplayBindings()||!Client->GetProfile().IsSet())return;
  GameKey(EKeys::I);break;
 case 1:
  if(!Check(C&&C->bPanel&&C->Panel==1&&Panel->IsActivated()&&PC->bShowMouseCursor&&!PC->IsPaused(),TEXT("Enhanced I opens visible inventory without pausing world")))return;
  IdleRefreshes=Panel->Model->RefreshCount;Capture(TEXT("Inventory.png"));break;
 case 2:
  if(!Check(Panel->Model->RefreshCount==IdleRefreshes,TEXT("Idle inventory does not rebuild snapshot every frame")))return;
  {
   const auto* Server=C->ProfileState()->GetNativeProfile();const auto& Published=Client->GetProfile();
   if(!Check(Server&&Published.IsSet()&&Server->Revision==Published->Revision&&
       Server->CharacterId==Published->CharacterId,TEXT("Native durable profile and owner snapshot agree")))return;
   if(!Check(Panel->GetClass()->GetPathName().StartsWith(TEXT("/Game/UI/Widgets/WBP_PlayerMenu")),
       TEXT("Formal authored Designer widget is active")))return;
  }
  if(!Check(FocusedKey(EKeys::J),TEXT("Focused button forwards journal shortcut through Slate")))return;
  Capture(TEXT("Journal.png"));
  break;
 case 3:
  if(!Check(C&&C->Panel==2&&Panel->IsActivated(),TEXT("Focused J switches page")))return;
  if(!Check(FocusedKey(EKeys::Escape),TEXT("Focused Escape is handled")))return;break;
 case 4:
  UE_LOG(LogTemp,Display,TEXT("V10_MENU_CLOSED open=%d page=%d active=%d cursor=%d menu=%d"),C?C->bPanel:0,C?C->Panel:0,Panel->IsActivated(),PC->bShowMouseCursor,Menu->IsOpen());
  if(!Check(C&&!C->bPanel&&!Panel->IsActivated()&&!PC->bShowMouseCursor,TEXT("Escape closes current page and restores game input")))return;
  GameKey(EKeys::I);break;
 case 5:
  if(!Check(C&&C->Panel==1&&Panel->IsActivated(),TEXT("Collapsed widget reopens from menu event")))return;
  if(!Check(FocusedKey(EKeys::I),TEXT("Same focused shortcut closes page")))return;break;
 case 6:
  if(!Check(C&&!C->bPanel&&!Panel->IsActivated(),TEXT("Repeated same shortcut closes")))return;
  GameKey(EKeys::Escape);break;
 case 7:
  if(!Check(C&&C->Panel==6&&Panel->IsActivated(),TEXT("Escape from gameplay opens system page")))return;
  if(!Check(FocusedKey(EKeys::M),TEXT("Focused map shortcut accepted")))return;break;
 case 8:
  if(!Check(C&&C->bPanel&&C->Panel==4&&PC->bShowMouseCursor&&Panel->IsActivated(),TEXT("Formal map shares menu input ownership")))return;
  C->StartJumpInput();C->SetSprintInput(true);
  if(!Check(!C->bPressedJump&&!C->bSprinting,TEXT("Map blocks local jump and sprint")))return;
  Capture(TEXT("Map.png"));GameKey(EKeys::Escape);break;
 case 9:
  if(!Check(C&&!C->bPanel&&!PC->bShowMouseCursor,TEXT("Map Escape restores game input")))return;
  C->OpenPanel(1);OldPawn=C;OldProfile=C->ProfileState();SpawnLocation=C->GetActorLocation();
  PC->UnPossess();break;
 case 10:
  if(!Check(!C&&!Panel->IsActivated()&&!PC->bShowMouseCursor&&OldPawn.IsValid()&&
      !OldPawn->OnPresentationChanged.IsBoundToObject(Panel)&&OldProfile.IsValid()&&
      !OldProfile->OnProfilePublished.IsBoundToObject(Panel),TEXT("Unpossess collapses UI and unsubscribes old Pawn and PlayerState")))return;
  {
   FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
   auto* Replacement=HUD->GetWorld()->SpawnActor<AAetherFrontierCharacter>(SpawnLocation+FVector(200,0,100),FRotator::ZeroRotator,Params);
   PC->Possess(Replacement);
  }
  break;
 case 11:
  // ClientRestart 会刷新按键并重建 Enhanced Input；等新输入上下文就绪再注入用户按键。
  if(!C||!C->ProfileState()||!C->HasGameplayBindings())return;
  GameKey(EKeys::I);break;
 case 12:
  UE_LOG(LogTemp,Display,TEXT("V10_MENU_REBIND pawn=%s open=%d page=%d visible=%d pawn_sub=%d profile_sub=%d old_sub=%d"),
      *GetNameSafe(C),C?C->bPanel:0,C?C->Panel:0,Panel->IsActivated(),
      C?C->OnPresentationChanged.IsBoundToObject(Panel):0,
      C&&C->ProfileState()?C->ProfileState()->OnProfilePublished.IsBoundToObject(Panel):0,
      OldPawn.IsValid()?OldPawn->OnPresentationChanged.IsBoundToObject(Panel):0);
  if(!Check(C&&C!=OldPawn.Get()&&C->bPanel&&Panel->IsActivated()&&
      Menu->GetBoundPawn()==C&&C->ProfileState()->OnProfilePublished.IsBoundToObject(Panel)&&
      !OldPawn->OnPresentationChanged.IsBoundToObject(Panel),TEXT("Replacement Pawn binds fresh UI context and can reopen inventory")))return;
  Capture(TEXT("NewPawn.png"));break;
 case 13: C->OpenPanel(3);break;
 case 14: Capture(TEXT("Skills.png"));break;
 case 15: C->OpenPanel(5);break;
 case 16: Capture(TEXT("Party.png"));break;
 case 17: C->OpenPanel(6);break;
 case 18: Capture(TEXT("Settings.png"));break;
 case 19: Menu->Close();break;
 case 20: GameKey(EKeys::Gamepad_Special_Left);break;
 case 21:
  if(!Check(C&&C->bPanel&&C->Panel==1,TEXT("Gamepad View opens inventory through Enhanced Input")))return;
  if(!Check(FocusedKey(EKeys::Gamepad_RightShoulder),TEXT("Gamepad shoulder handled by focused menu")))return;break;
 case 22: case 23: case 24: case 25: case 26:
  if(!Check(C&&C->Panel==Stage-20,TEXT("Gamepad shoulder cycles actual six pages")))return;
  if(!Check(FocusedKey(EKeys::Gamepad_RightShoulder),TEXT("Gamepad next page")))return;break;
 case 27:
  if(!Check(C&&C->Panel==1,TEXT("Gamepad page cycle wraps to inventory")))return;
  if(!Check(FocusedKey(EKeys::Gamepad_LeftShoulder),TEXT("Gamepad previous page")))return;break;
 case 28:
  if(!Check(C&&C->Panel==6,TEXT("Gamepad reverse page cycle wraps to system")))return;
  if(!Check(FocusedKey(EKeys::Gamepad_FaceButton_Right),TEXT("Gamepad back closes system")))return;break;
 case 29:
  if(!Check(C&&!C->bPanel&&!PC->bShowMouseCursor,TEXT("Gamepad back restores game input")))return;
  GameKey(EKeys::Gamepad_Special_Right);break;
 case 30:
  if(!Check(C&&C->Panel==6&&C->bPanel,TEXT("Gamepad Menu opens system through Enhanced Input")))return;
  if(!Check(FocusedKey(EKeys::Gamepad_FaceButton_Right),TEXT("Gamepad Menu return")))return;break;
 default:
  {
   if(FParse::Param(FCommandLine::Get(),TEXT("AetherClosureLight"))){TickClosureLight(HUD,Panel);return;}
   // 每一轮跨真实 Slate 帧开/关一次，验证 LocalPlayer 所有权和预览释放；没有新建测试 Widget。
   static int32 MenuCycles=0;
   if(MenuCycles>=200){
    auto* Preview=LP->GetSubsystem<UAetherCharacterPreviewSubsystem>();
    if(!Check(!Menu->IsOpen()&&!Preview->IsPreviewActive()&&!Preview->GetRenderTarget(),
        TEXT("100 menu cycles return preview and input ownership to closed state")))return;
    UE_LOG(LogTemp,Display,TEXT("V10_MENU_INTERACTION_PASS cycles=100 native=1"));FPlatformMisc::RequestExit(false);return;
   }
   if(MenuCycles%2==0)Menu->OpenPage(EAetherMenuPage::Inventory);else Menu->Close();
   ++MenuCycles;NextAt=Now+.05f;return;
  }
 }
 ++Stage;NextAt=Now+.8f;
#endif
}
