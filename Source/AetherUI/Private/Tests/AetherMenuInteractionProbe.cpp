#include "Tests/AetherMenuInteractionProbe.h"
#include "UI/AetherFrontierHUD.h"
#include "AetherFrontierPanel.h"
#include "AetherFrontier.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "InputKeyEventArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

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
  if(!C||!C->ProfileState()||!Panel->Model)return;
  GameKey(EKeys::I);break;
 case 1:
  if(!Check(C&&C->bPanel&&C->Panel==1&&Panel->IsVisible()&&PC->bShowMouseCursor&&!PC->IsPaused(),TEXT("Enhanced I opens visible inventory without pausing world")))return;
  IdleRefreshes=Panel->Model->RefreshCount;Capture(TEXT("Inventory.png"));break;
 case 2:
  if(!Check(Panel->Model->RefreshCount==IdleRefreshes,TEXT("Idle inventory does not rebuild snapshot every frame")))return;
  {
   auto P=C->ProfileState()->Profile;P.Add("ManaPotion",1);
   auto* Mode=HUD->GetWorld()->GetAuthGameMode<AAetherFrontierMode>();
   if(!Check(Mode&&Mode->Commit(C->ProfileState(),P)&&Panel->Model->Body.ToString().Contains(TEXT("ManaPotion")),TEXT("Real successful commit immediately publishes inventory snapshot")))return;
  }
  if(!Check(FocusedKey(EKeys::J),TEXT("Focused button forwards journal shortcut through Slate")))return;
  Capture(TEXT("Journal.png"));
  break;
 case 3:
  if(!Check(C&&C->Panel==2&&Panel->IsVisible(),TEXT("Focused J switches page")))return;
  if(!Check(FocusedKey(EKeys::Escape),TEXT("Focused Escape is handled")))return;break;
 case 4:
  if(!Check(C&&!C->bPanel&&!Panel->IsVisible()&&!PC->bShowMouseCursor,TEXT("Escape closes current page and restores game input")))return;
  GameKey(EKeys::I);break;
 case 5:
  if(!Check(C&&C->Panel==1&&Panel->IsVisible(),TEXT("Collapsed widget reopens from menu event")))return;
  if(!Check(FocusedKey(EKeys::I),TEXT("Same focused shortcut closes page")))return;break;
 case 6:
  if(!Check(C&&!C->bPanel&&!Panel->IsVisible(),TEXT("Repeated same shortcut closes")))return;
  GameKey(EKeys::Escape);break;
 case 7:
  if(!Check(C&&C->Panel==6&&Panel->IsVisible(),TEXT("Escape from gameplay opens system page")))return;
  if(!Check(FocusedKey(EKeys::M),TEXT("Focused map shortcut accepted")))return;break;
 case 8:
  if(!Check(C&&C->bPanel&&C->Panel==4&&PC->bShowMouseCursor&&!Panel->IsVisible(),TEXT("Legacy map shares menu input ownership")))return;
  C->StartJumpInput();C->SetSprintInput(true);
  if(!Check(!C->bPressedJump&&!C->bSprinting,TEXT("Map blocks local jump and sprint")))return;
  Capture(TEXT("Map.png"));GameKey(EKeys::Escape);break;
 case 9:
  if(!Check(C&&!C->bPanel&&!PC->bShowMouseCursor,TEXT("Map Escape restores game input")))return;
  C->OpenPanel(1);OldPawn=C;OldProfile=C->ProfileState();SpawnLocation=C->GetActorLocation();
  PC->UnPossess();break;
 case 10:
  if(!Check(!C&&!Panel->IsVisible()&&!PC->bShowMouseCursor&&OldPawn.IsValid()&&
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
      *GetNameSafe(C),C?C->bPanel:0,C?C->Panel:0,Panel->IsVisible(),
      C?C->OnPresentationChanged.IsBoundToObject(Panel):0,
      C&&C->ProfileState()?C->ProfileState()->OnProfilePublished.IsBoundToObject(Panel):0,
      OldPawn.IsValid()?OldPawn->OnPresentationChanged.IsBoundToObject(Panel):0);
  if(!Check(C&&C!=OldPawn.Get()&&C->bPanel&&Panel->IsVisible()&&
      C->OnPresentationChanged.IsBoundToObject(Panel)&&C->ProfileState()->OnProfilePublished.IsBoundToObject(Panel)&&
      !OldPawn->OnPresentationChanged.IsBoundToObject(Panel),TEXT("Replacement Pawn binds fresh UI context and can reopen inventory")))return;
  Capture(TEXT("NewPawn.png"));break;
 case 13:
  UE_LOG(LogTemp,Display,TEXT("V10_MENU_INTERACTION_PASS"));FPlatformMisc::RequestExit(false);return;
 }
 ++Stage;NextAt=Now+.8f;
#endif
}
