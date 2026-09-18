#include "Commands/AetherConsumableDeliveryPump.h"
namespace
{
bool SessionValid(const FAetherProfileSession& S)
{
    if(!S.SessionId.IsValid()||S.PawnEpoch==0||S.CharacterId.IsEmpty()||S.CharacterId.Len()>32)return false;
    for(TCHAR C:S.CharacterId)if(C<32)return false;
    FTCHARToUTF8 U(*S.CharacterId);FUTF8ToTCHAR Back(U.Get(),U.Length());return FString(Back.Length(),Back.Get())==S.CharacterId;
}
}
struct FAetherConsumableDeliveryPump::FImpl
{
    enum class EStage:uint8 { Idle, Resolve, Read, Deliver, Ack };
    TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store;
    FAetherProfileSession Session;
    FGuid ReceiverId;
    TFuture<FAetherStoreEffectsResult> ReadFuture;
    TFuture<bool> AckFuture;
    TArray<FAetherEffectDelivery> Batch;
    TArray<FGuid> PriorApplied;
    int32 Index=0;
    bool bFullRespawn=false,bPolling=false;
    EStage Stage=EStage::Idle;
    FAetherDeliveryPumpResult Result;
    explicit FImpl(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> S):Store(MoveTemp(S)){}
    FAetherDeliveryPumpResult Finish(EAetherDeliveryPumpCode Code)
    {
        Result.Code=Code;Stage=EStage::Idle;Batch.Reset();return Result;
    }
};
FAetherConsumableDeliveryPump::FAetherConsumableDeliveryPump(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store)
    :Impl(MakeUnique<FImpl>(MoveTemp(Store))){check(IsInGameThread());}
FAetherConsumableDeliveryPump::~FAetherConsumableDeliveryPump()
{
    check(IsInGameThread());
    // 丢弃未完成 ACK Future 不能撤销已经提交的删除；但 ACK 一定在资源应用之后才入队。
    // 消费者未收到删除成功时保留内存投递记录，重读或新生命恢复负责后续收口。
}
bool FAetherConsumableDeliveryPump::IsPending() const
{check(IsInGameThread());return Impl->Stage!=FImpl::EStage::Idle;}
bool FAetherConsumableDeliveryPump::Start(const FAetherProfileSession& S,bool FullRespawn)
{
    check(IsInGameThread()&&!Impl->bPolling);
    if(IsPending()||!SessionValid(S))return false;
    Impl->Session=S;Impl->ReceiverId.Invalidate();Impl->Result={};Impl->Result.Code=EAetherDeliveryPumpCode::Pending;
    Impl->Batch.Reset();Impl->Index=0;Impl->bFullRespawn=FullRespawn;
    Impl->PriorApplied.Reset();Impl->Stage=FImpl::EStage::Resolve;return true;
}
FAetherDeliveryPumpResult FAetherConsumableDeliveryPump::Poll(const FAetherResolveConsumableReceiver& Resolve)
{
    check(IsInGameThread()&&!Impl->bPolling);TGuardValue<bool> Guard(Impl->bPolling,true);
    using S=FImpl::EStage;using C=EAetherDeliveryPumpCode;
    if(Impl->Stage==S::Idle)return Impl->Result;
    auto* Receiver=Resolve?Resolve(Impl->Session):nullptr;
    if(!Receiver||!Receiver->CharacterId().Equals(Impl->Session.CharacterId,ESearchCase::CaseSensitive))return Impl->Finish(C::StaleSession);
    if(!Impl->ReceiverId.IsValid())Impl->ReceiverId=Receiver->InstanceId();
    else if(Impl->ReceiverId!=Receiver->InstanceId())return Impl->Finish(C::StaleSession);
    // 下面最多推进一个异步边界；每帧最多应用一条，避免恢复大批次霸占游戏线程。
    if(Impl->Stage==S::Resolve)
    {
        // 先采样已有去重记录，再入队完整读取；不会误删读取之后才新增的投递证明。
        Impl->PriorApplied=Receiver->PendingAcknowledgementIds();
        Impl->ReadFuture=Impl->Store->PendingEffects(Impl->Session.CharacterId);Impl->Stage=S::Read;
        return Impl->Result;
    }
    if(Impl->Stage==S::Read)
    {
        if(!Impl->ReadFuture.IsReady())return Impl->Result;
        auto Read=Impl->ReadFuture.Get();
        if(Read.Code!=EAetherStoreCode::Found)return Impl->Finish(C::StorageUnavailable);
        if(Read.Values.Num()>128)return Impl->Finish(C::Invalid);
        TSet<FGuid> Seen;
        for(const auto& D:Read.Values)
        {
            FAetherConsumableEffectV10 E;
            if(!D.ActorId.Equals(Impl->Session.CharacterId,ESearchCase::CaseSensitive)||D.SchemaVersion!=1||!AetherConsumableEffects::Decode(D.Payload,E)||
                E.DeliveryId!=D.Id||Seen.Contains(D.Id)){Impl->Result.BlockedDeliveryId=D.Id;return Impl->Finish(C::Invalid);}
            Seen.Add(D.Id);
        }
        // 整批校验在任何应用/ACK 之前完成；坏尾项不能造成前半批已删除、后半批被忽略。
        // 上一批 ACK 可能已成功，但其 Future 随断线被丢弃。完整读取证明它已不存在后
        // 才回收当时的去重槽，避免长期重连最终耗尽 128 个槽位。
        for(const auto& Id:Impl->PriorApplied)if(!Seen.Contains(Id))Receiver->ForgetAcknowledged(Id);
        Impl->Batch=MoveTemp(Read.Values);
        if(Impl->bFullRespawn&&!Receiver->RecoverAtFullRespawn(Impl->Batch,Impl->Session.CharacterId))return Impl->Finish(C::Conflict);
        Impl->Result.bFullRespawnRecovered=Impl->bFullRespawn;
        Impl->Stage=S::Deliver;
    }
    if(Impl->Stage==S::Ack)
    {
        if(!Impl->AckFuture.IsReady())return Impl->Result;
        if(!Impl->AckFuture.Get())return Impl->Finish(C::StorageUnavailable);
        Receiver->ForgetAcknowledged(Impl->Batch[Impl->Index].Id);++Impl->Result.Acknowledged;++Impl->Index;
        Impl->Stage=S::Deliver;
    }
    if(Impl->Stage==S::Deliver)
    {
        if(Impl->Index==Impl->Batch.Num()){Impl->Result.BlockedDeliveryId.Invalidate();return Impl->Finish(C::Complete);}
        const auto& D=Impl->Batch[Impl->Index];Impl->Result.BlockedDeliveryId=D.Id;
        const auto Applied=Receiver->Apply(D,Impl->Session.CharacterId);
        if(Applied!=EAetherEffectApplyCode::Applied&&Applied!=EAetherEffectApplyCode::Replayed)
            return Impl->Finish(Applied==EAetherEffectApplyCode::Invalid?C::Invalid:C::Conflict);
        Impl->AckFuture=Impl->Store->AcknowledgeEffect(Impl->Session.CharacterId,D.Id);Impl->Stage=S::Ack;
    }
    return Impl->Result;
}
