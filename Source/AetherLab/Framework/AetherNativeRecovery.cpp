#include "AetherFrontier.h"
#include "Framework/AetherPlayerController.h"
#include "Networking/AetherCommandRuntime.h"
#include "Inventory/AetherResourceGate.h"
#include "Engine/GameInstance.h"
bool AAetherFrontierMode::RecoverNativePlayer(AAetherFrontierCharacter* Pawn)
{
    if(!bNativeMode||!NativeSceneReady()||!IsValid(Pawn)||Pawn->Alive()||Pawn->TimeSinceDamage()<3||
       !Pawn->ResourceGate||Pawn->ResourceGate->IsBlocked())return false;
    auto* PC=Cast<AAetherPlayerController>(Pawn->GetController());
    auto* PS=PC?PC->GetPlayerState<AAetherPlayerState>():nullptr;
    if(!PC||!PS||!PS->GetNativeProfile()||!NativePlayersReady.Contains(PC))return false;
    // 新 Pawn 意味着新的资源生命和命令会话。治疗投递账本由统一恢复泵结算，
    // 不在旧 Pawn 上直接 SetVitals 跳过生命代次/遗留投递的恢复协议。
    auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>();
    Runtime->UnbindPlayer(PC);Pawn->CancelActions();Pawn->ReleaseCarry();
    ReleaseNativePawn(Pawn);PC->UnPossess();Pawn->Destroy();RestartPlayer(PC);
    return true;
}
