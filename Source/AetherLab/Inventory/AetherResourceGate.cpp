#include "Inventory/AetherResourceGate.h"
#include "AetherCombat.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"

UAetherResourceGate::UAetherResourceGate(){PrimaryComponentTick.bCanEverTick=true;}
FAetherResourceStateV10 UAetherResourceGate::Sample() const
{
    FAetherResourceStateV10 S=Receiver?Receiver->State():FAetherResourceStateV10();
    if(const auto* C=Cast<AAetherCharacter>(GetOwner()))
    {
        S.Health=C->Health();S.Mana=C->Mana();S.Stamina=C->Stamina();
        S.MaxHealth=C->MaxHealth;S.MaxMana=C->MaximumMana();S.MaxStamina=C->MaximumStamina();
    }
    return S;
}
bool UAetherResourceGate::BeginFullRespawn(const FString& Identity)
{
    check(IsInGameThread());auto* C=Cast<AAetherCharacter>(GetOwner());
    if(Receiver||bFaulted||!C||!C->HasAuthority()||!C->AbilitySystem||C->AbilitySystem->GetAvatarActor()!=C||
        Identity.IsEmpty()||Identity.Len()>32)return false;
    // 装备上限已由已提交记录恢复。明确建立新 LifeId，不能将旧生命治疗套到新生命。
    C->CancelActions();C->SetVitals(C->MaxHealth,C->MaximumMana(),C->MaximumStamina());
    auto S=Sample();S.LifeId=FGuid::NewGuid();S.Revision=0;S.UseReadyAtUnixMs=0;
    if(!S.Validate())return false;
    Receiver=MakeUnique<FAetherConsumableReceiver>(Identity,S);bRecovering=true;return true;
}
bool UAetherResourceGate::Synchronize()
{
    if(!Receiver||IsBlocked())return false;
    auto Next=Sample();const auto& Old=Receiver->State();
    if(Next.Same(Old))return true;
    if(Old.Revision>=MAX_int64-1){Fault(TEXT("Resource revision exhausted"));return false;}
    Next.Revision=Old.Revision+1;
    if(!Receiver->UpdateResources(Next)){Fault(TEXT("Resource owner changed outside its action barrier"));return false;}
    return true;
}
bool UAetherResourceGate::Reserve(FGuid Id,FAetherResourceStateV10& Before)
{
    if(IsBlocked()||!Deferred.IsEmpty()||bDraining||!Synchronize()||!Receiver->Reserve(Id,Before))return false;
    Reserved=Id;ReservedBefore=Before;return true;
}
bool UAetherResourceGate::CancelKnownUncommitted(FGuid Id)
{
    if(!Receiver||Reserved!=Id||!Receiver->CancelUncommitted(Id))return false;
    Reserved.Invalidate();ReservedBefore.Reset();return true;
}
bool UAetherResourceGate::Publish(const FAetherConsumableReceiver& Expected)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());
    if(!Receiver||Receiver.Get()!=&Expected||bFaulted||bPublishing||!C||!C->HasAuthority()||
        !C->AbilitySystem||C->AbilitySystem->GetAvatarActor()!=C||!Expected.State().Validate())return false;
    const auto S=Expected.State();
    if(Reserved.IsValid())
    {
        if(!ReservedBefore.IsSet())return false;
        auto Live=Sample();Live.Revision=ReservedBefore->Revision;Live.UseReadyAtUnixMs=ReservedBefore->UseReadyAtUnixMs;
        if(!Live.Same(ReservedBefore.GetValue()))
        {Fault(TEXT("ASC changed while a consumable transaction was reserved"));return false;}
    }
    if(!FMath::IsNearlyEqual(double(C->MaxHealth),S.MaxHealth,.001)||
        !FMath::IsNearlyEqual(double(C->MaximumMana()),S.MaxMana,.001)||
        !FMath::IsNearlyEqual(double(C->MaximumStamina()),S.MaxStamina,.001))return false;
    TGuardValue<bool> Guard(bPublishing,true);
    // 不调用延迟队列入口 SetVitals；属性通知引发的其他完整动作仍被 bPublishing 挡住。
    C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),float(S.Health));
    if(!IsValid(C)||C->AbilitySystem->GetAvatarActor()!=C)return false;
    C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetManaAttribute(),float(S.Mana));
    C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetStaminaAttribute(),float(S.Stamina));
    if(!IsValid(C)||C->AbilitySystem->GetAvatarActor()!=C)return false;
    if(!FMath::IsNearlyEqual(double(C->Health()),S.Health,.01)||!FMath::IsNearlyEqual(double(C->Mana()),S.Mana,.01)||
        !FMath::IsNearlyEqual(double(C->Stamina()),S.Stamina,.01))return false;
    Reserved.Invalidate();ReservedBefore.Reset();return true;
}
bool UAetherResourceGate::FinishRecovery()
{
    if(!Receiver||!bRecovering||Reserved.IsValid()||bFaulted||bPublishing)return false;
    bRecovering=false;TGuardValue<bool> Guard(bDraining,true);return Synchronize();
}
bool UAetherResourceGate::Defer(TUniqueFunction<void()> Action)
{
    if(!IsBlocked())return false;
    if(bFaulted)return true; // 已进入连接终止，不再接受这个 Pawn 的新动作。
    if(!Action)return true;
    if(Deferred.Num()>=256)
    {
        // 磁盘长时间不可用时不无限积压，也不解除屏障制造无敌/丢药。
        // 明确终止此连接，下次登录必须经过排空旧写者和完整新生命恢复。
        Fault(TEXT("Resource action backlog exceeded; reconnect after storage recovery"));return true;
    }
    Deferred.Add(MoveTemp(Action));return true;
}
void UAetherResourceGate::Fault(const FString& Reason)
{
    if(bFaulted)return;bFaulted=true;
    UE_LOG(LogTemp,Error,TEXT("AETHER_RESOURCE_SESSION_FAILED %s"),*Reason);
    const auto* C=Cast<AAetherCharacter>(GetOwner());const TWeakObjectPtr<APlayerController> PC=C?Cast<APlayerController>(C->GetController()):nullptr;
    if(GetWorld())GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[PC]{
        if(PC.IsValid())PC->ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("存储恢复未完成，请稍后重新连接。")));
    }));
}
void UAetherResourceGate::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);
    if(!Receiver||WaitingForPersistence()||bDraining)return;
    TGuardValue<bool> Guard(bDraining,true);
    // 每帧有界排空；恢复普通输入前 Reserve 仍要求队列为空，不能插队连续喝药。
    for(int32 Budget=0;Budget<32&&!Deferred.IsEmpty()&&!IsBlocked();++Budget)
    {
        auto Action=MoveTemp(Deferred[0]);Deferred.RemoveAt(0);Action();
        if(!IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed())break;
        if(!Synchronize())break;
    }
    if(!WaitingForPersistence())Synchronize();
}
void UAetherResourceGate::EndPlay(const EEndPlayReason::Type Reason)
{
    bFaulted=true;Deferred.Reset();Receiver.Reset();Super::EndPlay(Reason);
}
