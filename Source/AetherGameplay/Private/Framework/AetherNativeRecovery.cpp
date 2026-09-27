#include "Framework/AetherFrontier.h"
#include "Framework/AetherPlayerController.h"
#include "Networking/AetherCommandRuntime.h"
#include "Inventory/AetherResourceGate.h"
#include "Engine/GameInstance.h"
bool AAetherFrontierMode::CanRecoverNativePlayer(AAetherFrontierCharacter* Pawn,FString& Reason) const
{
    if(!IsValid(Pawn)||Pawn->Alive()){Reason=TEXT("角色已被救起");return false;}
    if(Pawn->TimeSinceDamage()<3){Reason=TEXT("等待救援，稍后可回据点");return false;}
    if(!bNativeMode||!NativeSceneReady()){Reason=TEXT("等待场景准备完成");return false;}
    if(Pawn->bTravelPending){Reason=TEXT("等待传送确认");return false;}
    if(!Pawn->ResourceGate||Pawn->ResourceGate->IsBlocked()){Reason=TEXT("等待资源与存储确认");return false;}
    auto* PC=Cast<AAetherPlayerController>(Pawn->GetController());
    auto* PS=PC?PC->GetPlayerState<AAetherPlayerState>():nullptr;
    if(!PC||PC->GetPawn()!=Pawn||!PS||!PS->GetNativeProfile()||!NativePlayersReady.Contains(PC))
    {Reason=TEXT("等待角色会话同步");return false;}
    Reason.Reset();return true;
}
bool AAetherFrontierMode::RecoverNativePlayer(AAetherFrontierCharacter* Pawn)
{
    FString Reason;if(!CanRecoverNativePlayer(Pawn,Reason))return false;
    auto* PC=CastChecked<AAetherPlayerController>(Pawn->GetController());
    // 新 Pawn 意味着新的资源生命和命令会话。治疗投递账本由统一恢复泵结算，
    // 不在旧 Pawn 上直接 SetVitals 跳过生命代次/遗留投递的恢复协议。
    auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>();
    Runtime->UnbindPlayer(PC);Pawn->CancelActions();Pawn->ReleaseCarry();
    ReleaseNativePawn(Pawn);PC->UnPossess();Pawn->Destroy();RestartPlayer(PC);
    return true;
}
