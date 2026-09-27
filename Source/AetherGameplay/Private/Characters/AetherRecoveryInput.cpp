#include "Characters/AetherFrontierCharacter.h"
#include "Framework/AetherFrontier.h"
#include "Inventory/AetherResourceGate.h"
#include "GameFramework/PlayerController.h"

void AAetherFrontierCharacter::UpdateRecoveryState()
{
    if(Alive()||RecoveryRequestLife!=RecoveryLife){bRecoveryRequested=false;RecoveryRequestLife.Invalidate();}
    if(!HasAuthority())return;
    if(Alive())
    {
        bWasDowned=false;RecoveryLife.Invalidate();bRecoveryAvailable=false;RecoveryWait=0;RecoveryReason.Reset();return;
    }
    if(!bWasDowned){bWasDowned=true;RecoveryLife=FGuid::NewGuid();}
    RecoveryWait=FMath::Max(0.f,3.f-TimeSinceDamage());
    auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();
    if(Mode&&Mode->IsNativeMode())bRecoveryAvailable=Mode->CanRecoverNativePlayer(this,RecoveryReason);
    else
    {
        bRecoveryAvailable=Mode&&ProfileState()&&RecoveryWait<=0&&ResourceGate&&!ResourceGate->IsBlocked()&&!bTravelPending;
        RecoveryReason=bRecoveryAvailable?FString():TEXT("等待救援、资源或场景同步");
    }
}
void AAetherFrontierCharacter::Recover()
{
    const auto* PC=Cast<APlayerController>(Controller);
    if(!PC||!PC->IsLocalController()||PC->GetPawn()!=this||Alive()||bRecoveryRequested||!bRecoveryAvailable||!RecoveryLife.IsValid())return;
    bRecoveryRequested=true;RecoveryRequestLife=RecoveryLife;ServerRecover(RecoveryLife);
}
void AAetherFrontierCharacter::ServerRecover_Implementation(FGuid Life)
{
    UpdateRecoveryState();
    auto* PC=Cast<APlayerController>(Controller);
    if(!PC||PC->GetPawn()!=this||!Life.IsValid()||Life!=RecoveryLife||Alive())
    {ClientRecoveryResult(Life,TEXT("本次倒地状态已结束"));return;}
    if(!bRecoveryAvailable){ClientRecoveryResult(Life,RecoveryReason);return;}
    auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();
    if(Mode&&Mode->IsNativeMode())
    {
        if(!Mode->RecoverNativePlayer(this))ClientRecoveryResult(Life,TEXT("恢复条件已变化，请等待同步后重试"));
        return; // 成功由新 Pawn 与新命令会话的复制确认。
    }
    auto* PS=ProfileState();if(!Mode||!PS){ClientRecoveryResult(Life,TEXT("角色会话不可用"));return;}
    ReleaseCarry();SetVitals(MaxHealth,100,100);ResetCombat();
    BeginSafeTravel(PS->Profile.bRegistered?FVector(-500,-500,120):FVector(-6500,-29000,120));
}
void AAetherFrontierCharacter::ClientRecoveryResult_Implementation(FGuid Life,const FString& Reason)
{
    if(Life!=RecoveryRequestLife)return;
    bRecoveryRequested=false;RecoveryRequestLife.Invalidate();Feedback=Reason;OnPresentationChanged.Broadcast();
}
