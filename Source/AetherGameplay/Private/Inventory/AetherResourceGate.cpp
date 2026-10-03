#include "Inventory/AetherResourceGate.h"
#include "Combat/AetherCombat.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Misc/DateTime.h"
#include "Effects/AetherBuffRuntime.h"

namespace
{
// GAS 属性通知和取消动作都可同步重入。整批资源写入只属于进入时的投影目标。
struct FResourceProjectionTarget
{
    TWeakObjectPtr<AAetherCharacter> Character;
    TWeakObjectPtr<UAbilitySystemComponent> System;
    TWeakObjectPtr<UAetherAttributes> Attributes;
    TWeakObjectPtr<AActor> SystemOwner;
    explicit FResourceProjectionTarget(AAetherCharacter* C)
        :Character(C),System(C->AbilitySystem.Get()),Attributes(C->Attributes.Get()),SystemOwner(System->GetOwnerActor()){}
    bool IsCurrent(const UAetherResourceGate* Gate) const
    {
        const auto* C=Character.Get();const auto* ASC=System.Get();
        return C&&!C->IsActorBeingDestroyed()&&C->HasAuthority()&&C->ResourceGate.Get()==Gate&&!Gate->IsFaulted()&&
            ASC&&Attributes.IsValid()&&C->AbilitySystem.Get()==ASC&&C->Attributes.Get()==Attributes.Get()&&
            ASC->GetOwnerActor()==SystemOwner.Get()&&ASC->GetAvatarActor()==C;
    }
};
}

UAetherResourceGate::UAetherResourceGate(){PrimaryComponentTick.bCanEverTick=true;SetIsReplicatedByDefault(true);}
void UAetherResourceGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UAetherResourceGate,bUseSummaryReady,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UAetherResourceGate,bSlowStorage,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UAetherResourceGate,UseReadyAtServerTime,COND_OwnerOnly);
}
FAetherResourceGateMetrics UAetherResourceGate::Inspect() const
{
    FAetherResourceGateMetrics M;M.DeferredCount=Deferred.Num();M.DeferredTotal=DeferredTotal;
    M.RecoveryCount=RecoveryCount;M.PublicationFailures=PublicationFailures;
    M.MergedIntervals=MergedIntervals;M.HighWater=HighWater;
    const double Now=FPlatformTime::Seconds();
    M.OldestWaitSeconds=Deferred.IsEmpty()?0:FMath::Max(0.,Now-Deferred[0].QueuedAt);
    M.PersistenceWaitSeconds=PersistenceWaitStarted>0?FMath::Max(0.,Now-PersistenceWaitStarted):0;
    return M;
}
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
    if(Receiver||bFaulted||bPublishing||!C||!C->HasAuthority()||!C->AbilitySystem||C->AbilitySystem->GetAvatarActor()!=C||
        Identity.IsEmpty()||Identity.Len()>32)return false;
    const FResourceProjectionTarget Target(C);if(!Target.IsCurrent(this))return false;
    // 取消动作前就建立重入屏障；否则取消回调能在 Receiver 尚未建立时再次开始重生。
    TGuardValue<bool> Guard(bPublishing,true);
    const auto Current=[&]{
        if(!Receiver&&Target.IsCurrent(this))return true;
        Fault(TEXT("Resource projection target changed during initialization"));return false;
    };
    // 装备上限已由已提交记录恢复。明确建立新 LifeId，不能将旧生命治疗套到新生命。
    C->CancelActions();if(!Current())return false;
    Target.System->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),C->MaxHealth);
    if(!Current())return false;
    Target.System->SetNumericAttributeBase(UAetherAttributes::GetManaAttribute(),C->MaximumMana());
    if(!Current())return false;
    Target.System->SetNumericAttributeBase(UAetherAttributes::GetStaminaAttribute(),C->MaximumStamina());
    if(!Current())return false;
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
    Reserved=Id;ReservedBefore=Before;bReservedResourcesPublished=false;return true;
}
bool UAetherResourceGate::CancelKnownUncommitted(FGuid Id)
{
    if(!Receiver||Reserved!=Id||!Receiver->CancelUncommitted(Id))return false;
    Reserved.Invalidate();ReservedBefore.Reset();bReservedResourcesPublished=false;return true;
}
bool UAetherResourceGate::Publish(const FAetherConsumableReceiver& Expected)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());
    if(!Receiver||Receiver.Get()!=&Expected||bFaulted||bPublishing||!C||!C->HasAuthority()||
        !C->AbilitySystem||C->AbilitySystem->GetAvatarActor()!=C||!Expected.State().Validate())return false;
    const auto S=Expected.State();
    const auto Effects=Expected.AppliedEffects();
    const FResourceProjectionTarget Target(C);const FGuid ReceiverId=Expected.InstanceId();
    const auto Current=[&]{
        if(Target.IsCurrent(this)&&Receiver&&Receiver->InstanceId()==ReceiverId&&Receiver->State().LifeId==S.LifeId)return true;
        // 不允许下一次投递重试把同一旧 Receiver 的 After 值绑定到替换后的 ASC。
        Fault(TEXT("Resource projection target changed during delivery"));return false;
    };
    if(!Current())return false;
    if(!Reserved.IsValid()&&!bRecovering)
    {
        bool Published=true;for(const auto& E:Effects)Published&=PublishedDeliveries.Contains(E.DeliveryId);
        if(Published)return true;
    }
    if(Reserved.IsValid()&&!bReservedResourcesPublished)
    {
        if(!ReservedBefore.IsSet())return false;
        auto Live=Sample();Live.Revision=ReservedBefore->Revision;Live.UseReadyAtUnixMs=ReservedBefore->UseReadyAtUnixMs;
        if(!Live.Same(ReservedBefore.GetValue()))
        {Fault(TEXT("ASC changed while a consumable transaction was reserved"));return false;}
    }
    if(!bReservedResourcesPublished&&(!FMath::IsNearlyEqual(double(C->MaxHealth),S.MaxHealth,.001)||
        !FMath::IsNearlyEqual(double(C->MaximumMana()),S.MaxMana,.001)||
        !FMath::IsNearlyEqual(double(C->MaximumStamina()),S.MaxStamina,.001))){++PublicationFailures;return false;}
    TGuardValue<bool> Guard(bPublishing,true);
    if(!bReservedResourcesPublished)
    {
    // 不调用延迟队列入口 SetVitals；属性通知引发的其他完整动作仍被 bPublishing 挡住。
    Target.System->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),float(S.Health));
    if(!Current())return false;
    Target.System->SetNumericAttributeBase(UAetherAttributes::GetManaAttribute(),float(S.Mana));
    if(!Current())return false;
    Target.System->SetNumericAttributeBase(UAetherAttributes::GetStaminaAttribute(),float(S.Stamina));
    if(!Current())return false;
    if(!FMath::IsNearlyEqual(double(C->Health()),S.Health,.01)||!FMath::IsNearlyEqual(double(C->Mana()),S.Mana,.01)||
        !FMath::IsNearlyEqual(double(C->Stamina()),S.Stamina,.01))return false;
        bReservedResourcesPublished=true;
    }
    {
        TGuardValue<bool> ProjectionGuard(bEffectProjection,true);
        for(const auto& Effect:Effects)if(!Effect.BuffId.IsEmpty())
        {
            const bool Applied=C->BuffRuntime&&C->BuffRuntime->ApplyDelivery(Effect);
            if(!Current()||!Applied)return false;
        }
    }
    if(!Current())return false;
    PublishedDeliveries.Reset();for(const auto& E:Effects)PublishedDeliveries.Add(E.DeliveryId);
    Reserved.Invalidate();ReservedBefore.Reset();bReservedResourcesPublished=false;return true;
}
bool UAetherResourceGate::FinishRecovery()
{
    if(!Receiver||!bRecovering||Reserved.IsValid()||bFaulted||bPublishing)return false;
    bRecovering=false;++RecoveryCount;TGuardValue<bool> Guard(bDraining,true);return Synchronize();
}
bool UAetherResourceGate::Defer(TUniqueFunction<void()> Action,EAetherEffectEventKind Kind)
{
    if(!IsBlocked())return false;
    if(bFaulted)return true; // 已进入连接终止，不再接受这个 Pawn 的新动作。
    if(!Action)return true;
    if(Kind==EAetherEffectEventKind::ProjectionRefresh&&Deferred.ContainsByPredicate([](const auto& E){return E.Identity.Kind==EAetherEffectEventKind::ProjectionRefresh;}))return true;
    if(Deferred.Num()>=256)
    {
        // 磁盘长时间不可用时不无限积压，也不解除屏障制造无敌/丢药。
        // 明确终止此连接，下次登录必须经过排空旧写者和完整新生命恢复。
        Fault(TEXT("Resource action backlog exceeded; reconnect after storage recovery"));return true;
    }
    FDeferredAction Entry;Entry.Action=MoveTemp(Action);Entry.QueuedAt=FPlatformTime::Seconds();
    Entry.Identity.Sequence=++NextSequence;Entry.Identity.LogicalTime=Entry.QueuedAt;Entry.Identity.Kind=Kind;
    if(Receiver)Entry.Identity.LifeId=Receiver->State().LifeId;
    Deferred.Add(MoveTemp(Entry));++DeferredTotal;HighWater=FMath::Max(HighWater,Deferred.Num());return true;
}
bool UAetherResourceGate::DeferInterval(const FAetherResourceAdvanceInterval& Interval,TUniqueFunction<void(double)> Apply)
{
    check(IsInGameThread());
    if(!IsBlocked())return false;
    if(bFaulted)return true;
    if(!FMath::IsFinite(Interval.Start)||!FMath::IsFinite(Interval.End)||Interval.End<Interval.Start||!Apply)
    {Fault(TEXT("Invalid resource interval"));return true;}
    ++DeferredTotal;
    if(Interval.bPreserveSteps&&PendingSteps>=4096){Fault(TEXT("Resource step budget exceeded"));return true;}
    if(!Deferred.IsEmpty()&&Deferred.Last().Interval.IsSet()&&Deferred.Last().Interval->CanMerge(Interval))
    {
        Deferred.Last().Interval->End=Interval.End;
        if(Interval.bPreserveSteps){Deferred.Last().Steps.Add(Interval.End-Interval.Start);++PendingSteps;}
        ++MergedIntervals;return true;
    }
    if(Deferred.Num()>=256){Fault(TEXT("Resource event backlog exceeded"));return true;}
    FDeferredAction Entry;Entry.QueuedAt=FPlatformTime::Seconds();Entry.Interval=Interval;
    Entry.Identity=Interval.Identity;Entry.Identity.Kind=EAetherEffectEventKind::ResourceAdvanceInterval;
    Entry.Identity.Sequence=++NextSequence;Entry.ApplyInterval=MoveTemp(Apply);
    if(Interval.bPreserveSteps){Entry.Steps.Add(Interval.End-Interval.Start);++PendingSteps;}
    Deferred.Add(MoveTemp(Entry));HighWater=FMath::Max(HighWater,Deferred.Num());return true;
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
    if(const auto* C=Cast<AAetherCharacter>(GetOwner());C&&C->HasAuthority())
    {
        const double Now=FPlatformTime::Seconds();
        if(WaitingForPersistence())
        {
            if(PersistenceWaitStarted<=0)PersistenceWaitStarted=Now;
            bSlowStorage=Now-PersistenceWaitStarted>=2.;
            // 终止旧生命的入口，不取消数据库 Future、不释放不确定预留；写者仍由 Runtime 排空。
            if(Now-PersistenceWaitStarted>=30.&&!bFaulted)Fault(TEXT("Persistence confirmation timed out; session recovery required"));
        }
        else {PersistenceWaitStarted=0;bSlowStorage=false;}
        bUseSummaryReady=Receiver.IsValid()&&!IsBlocked();
        if(Receiver&&PublishedUseDeadline!=Receiver->State().UseReadyAtUnixMs)
        {
            PublishedUseDeadline=Receiver->State().UseReadyAtUnixMs;
            const auto Time=FDateTime::UtcNow();const int64 UnixNowMs=Time.ToUnixTimestamp()*1000+Time.GetMillisecond();
            UseReadyAtServerTime=C->CombatTime()+float(FMath::Max<int64>(0,PublishedUseDeadline-UnixNowMs))*.001f;
        }
    }
    if(!Receiver||WaitingForPersistence()||bDraining)return;
    TGuardValue<bool> Guard(bDraining,true);
    // 每帧有界排空；恢复普通输入前 Reserve 仍要求队列为空，不能插队连续喝药。
    for(int32 Budget=0;Budget<32&&!Deferred.IsEmpty()&&!IsBlocked();++Budget)
    {
        auto Entry=MoveTemp(Deferred[0]);Deferred.RemoveAt(0);
        if(Entry.Identity.LifeId.IsValid()&&Entry.Identity.LifeId!=Receiver->State().LifeId)
        {PendingSteps-=Entry.Steps.Num()-Entry.NextStep;continue;}
        if(Entry.NextStep<Entry.Steps.Num())
        {
            const double Step=Entry.Steps[Entry.NextStep++];--PendingSteps;Entry.ApplyInterval(Step);
            if(Entry.NextStep<Entry.Steps.Num())Deferred.Insert(MoveTemp(Entry),0);
        }
        else if(Entry.Interval.IsSet())Entry.ApplyInterval(Entry.Interval->End-Entry.Interval->Start);
        else if(Entry.Action)Entry.Action();
        if(!IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed())break;
        if(!Synchronize())break;
    }
    if(!WaitingForPersistence())Synchronize();
}
void UAetherResourceGate::EndPlay(const EEndPlayReason::Type Reason)
{
    bFaulted=true;Deferred.Reset();Receiver.Reset();Super::EndPlay(Reason);
}
