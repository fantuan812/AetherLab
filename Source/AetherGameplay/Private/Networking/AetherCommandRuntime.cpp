#include "Networking/AetherCommandRuntime.h"
#include "Framework/AetherPlayerController.h"
#include "Framework/AetherProgression.h"
#include "Definitions/AetherV10Definitions.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherContainerCodec.h"
#include "Inventory/AetherResourceGate.h"
#include "Commands/AetherConsumableDeliveryPump.h"
#include "Misc/DateTime.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Crc.h"

struct FAetherCommandRuntimeImpl
{
    struct FBinding
    {
        TWeakObjectPtr<AAetherPlayerController> Controller;
        TWeakObjectPtr<AAetherPlayerState> PlayerState;
        TWeakObjectPtr<APawn> Pawn;
        FAetherProfileSession Session;
        FGuid Channel;
        uint64 SceneSequence=0;
        FAetherV10RequestBudget Requests,Syncs;
        TFuture<FAetherStoreReadResult> Read;
        TArray<uint8> Outgoing;
        FGuid Transfer,InFlightTransfer;
        uint32 InFlightEnd=0;
        int64 Revision=-1;
        uint32 Offset=0,Checksum=0;
        bool bReady=false,bNeedsRecovery=true,bNeedFullRecovery=true,bDeliveryRequested=false;
        TUniquePtr<FAetherConsumableDeliveryPump> Delivery;
        TOptional<FAetherPlayerCommand> ResourceCommand;
        TFuture<FAetherStoreResult> Proof;
        TFuture<FAetherStoreReadResult> ProofRead;
        double NextDelivery=0;
        FString ContainerId;
        FGuid ContainerContext,ContainerTransfer;
        TFuture<FAetherStoreSnapshotResult> ContainerRead;
        TArray<uint8> ContainerOutgoing;
        int64 ContainerRevision=-1,ContainerWorldRevision=-1;
        uint32 ContainerOffset=0,ContainerChecksum=0;
        bool bContainerDirty=false,bPreferContainer=false;
        double NextContainerRead=0;
    };
    TSharedPtr<IAetherTransactionalStore,ESPMode::ThreadSafe> Store;
    TUniquePtr<FAetherProfileCoordinator> Coordinator;
    TUniquePtr<FAetherServerFactCoordinator> Facts;
    TArray<FAetherServerFact> DeferredFacts;
    FAetherResolveConnectedContext Resolve;
    FAetherPublishConnectedState Publish;
    TFunction<void(const FAetherWorldStateV10&)> PublishWorld;
    TFunction<void(const FAetherContainerStateV10&)> PublishContainer;
    TFunction<bool(AAetherPlayerController&,const FString&,bool)> AuthorizeContainer;
    TMap<TWeakObjectPtr<AAetherPlayerController>,TUniquePtr<FBinding>> Bindings;
    int32 NextSender=0;
    float SendElapsed=0;
    bool bPolling=false,bTicking=false,bShutdownRequested=false;
    FGuid Realm;
    bool HasPendingFacts(const FString& Id) const
    {return Facts->HasPendingForCharacter(Id)||DeferredFacts.ContainsByPredicate([&](const auto& E){return E.CharacterId.Equals(Id,ESearchCase::CaseSensitive);});}
    void PumpFacts()
    {
        while(!DeferredFacts.IsEmpty())
        {
            FString Why;if(!Facts->Enqueue(DeferredFacts[0],Why))
            {
                if(Why.Contains(TEXT("queue full")))return;
                UE_LOG(LogTemp,Error,TEXT("AETHER_DEFERRED_FACT_INVALID %s"),*Why);
                for(auto& Pair:Bindings)if(Pair.Value->Session.CharacterId.Equals(DeferredFacts[0].CharacterId,ESearchCase::CaseSensitive))
                    if(auto* G=Gate(*Pair.Value))G->Fault(TEXT("Retained server fact was rejected"));
            }
            DeferredFacts.RemoveAt(0);
        }
    }
    bool Current(const FBinding& B) const
    {
        const auto* C=B.Controller.Get();const auto* PS=B.PlayerState.Get();
        return !bShutdownRequested&&C&&PS&&!C->IsActorBeingDestroyed()&&!PS->IsActorBeingDestroyed()&&C->HasAuthority()&&C->GetPlayerState<AAetherPlayerState>()==PS&&C->GetPawn()==B.Pawn.Get()&&B.Pawn.IsValid()&&!B.Pawn->IsActorBeingDestroyed()&&
            PS->Profile.CharacterId.Equals(B.Session.CharacterId,ESearchCase::CaseSensitive)&&Coordinator&&Coordinator->IsCurrent(B.Session);
    }
    UAetherResourceGate* Gate(const FBinding& B) const
    {const auto* C=Cast<AAetherCharacter>(B.Pawn.Get());return C?C->ResourceGate.Get():nullptr;}
    void Queue(FBinding& B,const FAetherProfileStateV10& P,const FAetherWorldStateV10* World=nullptr,const FAetherContainerStateV10* Container=nullptr)
    {
        if(!Current(B)||!P.CharacterId.Equals(B.Session.CharacterId,ESearchCase::CaseSensitive)||P.Revision<B.Revision)return;
        // 先发布持久事实，失败保持未就绪，不能让界面领先于角色实际能力。
        B.bReady=false;B.Outgoing.Reset();B.Revision=P.Revision;
        FString Reason;
        {
            TGuardValue<bool> Guard(bPolling,true);
            if(!Publish(*B.Controller.Get(),P,World,Container)||!Current(B))return;
            // 完整场景/装备适配通过后，把已提交永久技能写入本连接的持续 ASC。
            // 外部来源由服务器适配器先行重建；这里保留它们，不用空数组抹掉装备/临时授权。
            if(!B.PlayerState->PublishNativeEquipment(P,Reason)||!Current(B))return;
            const auto Grants=B.PlayerState->GetNativeSkillGrants();
            if(!B.PlayerState->PublishNativeSkills(P,Grants,Reason)||!Current(B))return;
            auto* Resources=Gate(B);if(!Resources)return;
            if(B.bNeedsRecovery)
            {
                if(!Resources->IsEnabled()&&!Resources->BeginFullRespawn(B.Session.CharacterId))return;
                if(!Resources->IsRecovering()){Resources->Fault(TEXT("Rebound avatar requires a fresh resource life"));return;}
                B.bDeliveryRequested=true;
            }
        }
        const auto& D=FAetherV10Definitions::Get();TArray<uint8> Bytes;
        if(!AetherProfileCodec::Encode(P,D.Items,D.Skills,D.Rules,Bytes,Reason))return;
        // 同一连接最多一份待发送快照，新提交替换尚未发完的旧快照；客户端用 Transfer 区分。
        B.Outgoing=MoveTemp(Bytes);B.Transfer=FGuid::NewGuid();B.Revision=P.Revision;B.Offset=0;
        B.Checksum=FCrc::MemCrc32(B.Outgoing.GetData(),B.Outgoing.Num());B.bReady=!B.bNeedsRecovery;
    }
    void CloseContainer(FBinding& B)
    {
        const auto Old=B.ContainerContext;
        B.ContainerContext.Invalidate();B.ContainerId.Reset();B.ContainerRead={};B.ContainerOutgoing.Reset();
        B.bContainerDirty=false;B.ContainerRevision=-1;B.ContainerWorldRevision=-1;
        if(auto* C=B.Controller.Get())
        {
            if(AuthorizeContainer)AuthorizeContainer(*C,{},false);
            if(Old.IsValid())C->ClientV10ContainerClosed(B.Channel,Old);
        }
    }
    void PollContainer(FBinding& B)
    {
        if(!B.ContainerContext.IsValid())return;
        // 授权不随异步读取自动延长，离开范围/换场景立刻清掉发送队列。
        if(!Current(B)||!AuthorizeContainer||!AuthorizeContainer(*B.Controller.Get(),B.ContainerId,false))
        {CloseContainer(B);return;}
        const double Now=FPlatformTime::Seconds();
        if(B.ContainerRead.IsValid()&&B.ContainerRead.IsReady())
        {
            auto R=B.ContainerRead.Get();B.ContainerRead={};
            const auto* Row=R.Values.Find({EAetherAggregateKind::Container,B.ContainerId});
            const auto* World=R.Values.Find({EAetherAggregateKind::World,TEXT("Main")});
            FAetherContainerStateV10 Candidate;FString Why;const auto& D=FAetherV10Definitions::Get();
            if(R.Code==EAetherStoreCode::Busy||R.Code==EAetherStoreCode::Unavailable)
            {B.bContainerDirty=true;B.NextContainerRead=Now+1;}
            else if(R.Code!=EAetherStoreCode::Found||!Row||!World||Row->SchemaVersion!=10||World->Revision<0||
                !AetherContainerCodec::Decode(Row->Payload,D.Items,Candidate,Why)||Candidate.Revision!=Row->Revision||
                !Candidate.ContainerId.Equals(B.ContainerId,ESearchCase::CaseSensitive)||!Candidate.bActive||!Candidate.Allows(B.Session.CharacterId))
            {CloseContainer(B);return;}
            else if(Candidate.Revision>=B.ContainerRevision)
            {
                B.ContainerOutgoing=Row->Payload;B.ContainerTransfer=FGuid::NewGuid();B.ContainerOffset=0;
                B.ContainerChecksum=FCrc::MemCrc32(B.ContainerOutgoing.GetData(),B.ContainerOutgoing.Num());
                B.ContainerRevision=Candidate.Revision;B.ContainerWorldRevision=World->Revision;
            }
        }
        if(B.bContainerDirty&&!B.ContainerRead.IsValid()&&Now>=B.NextContainerRead)
        {
            FAetherStoreSnapshotQuery Q;Q.Keys={{EAetherAggregateKind::Container,B.ContainerId},{EAetherAggregateKind::World,TEXT("Main")}};
            B.ContainerRead=Store->ReadSnapshot(MoveTemp(Q));B.bContainerDirty=false;B.NextContainerRead=Now+.25;
        }
    }
    void BeginProof(FBinding& B)
    {
        if(!B.ResourceCommand.IsSet()||B.Proof.IsValid()||B.ProofRead.IsValid())return;
        FAetherReceiptQuery Q;Q.ActorId=B.Session.CharacterId;Q.CommandId=B.ResourceCommand->CommandId;
        Q.ProtocolVersion=B.ResourceCommand->ProtocolVersion;FString Why;
        if(!AetherCommands::Encode(*B.ResourceCommand,Q.Request,Why)){if(auto* G=Gate(B))G->Fault(TEXT("Reserved request cannot be encoded"));return;}
        B.Proof=Store->LookupReceipt(MoveTemp(Q));
    }
    void FinishResourceCommand(FBinding& B,const FAetherProfileCompletion& Completion)
    {
        if(Completion.CommandType!=EAetherCommandType::UseItem)return;
        auto* G=Gate(B);if(!G)return;
        if(Completion.CommitCertainty==EAetherCommitCertainty::Committed)
        {B.bDeliveryRequested=true;return;}
        if(!G->Reservation().IsValid())return;
        if((Completion.CommitCertainty==EAetherCommitCertainty::NotSubmitted&&Completion.bReservedResources)||Completion.CommitCertainty==EAetherCommitCertainty::NotCommitted)
        {G->CancelKnownUncommitted(Completion.Result.CommandId);B.ResourceCommand.Reset();return;}
        // 结果不可确定时继续持有屏障；超时本身不是回滚证明。
        BeginProof(B);
    }
    void PollResources(FBinding& B)
    {
        if(!Current(B))return;auto* G=Gate(B);if(!G||!G->IsEnabled())return;
        const double Now=FPlatformTime::Seconds();
        if(B.Proof.IsValid()&&B.Proof.IsReady())
        {
            auto R=B.Proof.Get();B.Proof={};
            if(R.Code==EAetherStoreCode::Replayed||R.Code==EAetherStoreCode::Committed)B.bDeliveryRequested=true;
            else if(R.Code==EAetherStoreCode::Missing)
                B.ProofRead=Store->Read({EAetherAggregateKind::Profile,B.Session.CharacterId});
            else if(R.Code==EAetherStoreCode::Busy||R.Code==EAetherStoreCode::Unavailable)B.NextDelivery=Now+1;
            else G->Fault(TEXT("Cannot resolve reserved resource transaction"));
        }
        if(B.ProofRead.IsValid()&&B.ProofRead.IsReady())
        {
            auto R=B.ProofRead.Get();B.ProofRead={};
            const auto& D=FAetherV10Definitions::Get();FAetherProfileStateV10 Profile;FString Why;
            if(R.Code==EAetherStoreCode::Found&&R.Value.IsSet()&&R.Value->SchemaVersion==10&&B.ResourceCommand.IsSet()&&
                AetherProfileCodec::Decode(R.Value->Payload,D.Items,D.Skills,D.Rules,Profile,Why)&&
                Profile.CharacterId.Equals(B.Session.CharacterId,ESearchCase::CaseSensitive)&&Profile.Revision==R.Value->Revision&&
                R.Value->Revision==B.ResourceCommand->ExpectedProfileRevision)
            {
                if(!G->CancelKnownUncommitted(B.ResourceCommand->CommandId))G->Fault(TEXT("Reservation changed during commit proof"));
                B.ResourceCommand.Reset();
            }
            else G->Fault(TEXT("Receipt missing after profile advanced; full recovery required"));
        }
        if(B.ResourceCommand.IsSet()&&G->Reservation().IsValid()&&!B.bDeliveryRequested&&
            !B.Proof.IsValid()&&!B.ProofRead.IsValid()&&Now>=B.NextDelivery&&
            !Coordinator->HasPendingForCharacter(B.Session.CharacterId))BeginProof(B);
        if(!B.Delivery)B.Delivery=MakeUnique<FAetherConsumableDeliveryPump>(Store.ToSharedRef());
        if(B.bDeliveryRequested&&!B.Delivery->IsPending()&&Now>=B.NextDelivery)
            B.Delivery->Start(B.Session,B.bNeedFullRecovery);
        if(!B.Delivery->IsPending())return;
        const auto R=B.Delivery->Poll([&](const FAetherProfileSession& S)->FAetherConsumableReceiver*{
            if(!(S==B.Session)||!Current(B))return nullptr;
            auto* Owner=Gate(B);if(!Owner)return nullptr;
            if(!Owner->IsBlocked()&&!Owner->Synchronize())return nullptr;
            return Owner->GetReceiver();
        },[&](const FAetherProfileSession& S,const FAetherConsumableReceiver& Receiver){
            auto* Owner=Gate(B);return S==B.Session&&Current(B)&&Owner&&Owner->Publish(Receiver)&&Current(B);
        });
        if(R.bFullRespawnRecovered)B.bNeedFullRecovery=false;
        if(R.Code==EAetherDeliveryPumpCode::Complete)
        {
            if(G->Reservation().IsValid()){G->Fault(TEXT("Committed resource delivery is missing"));return;}
            B.bDeliveryRequested=false;B.ResourceCommand.Reset();
            if(B.bNeedsRecovery)
            {
                if(!G->FinishRecovery()){G->Fault(TEXT("Unable to finish full resource recovery"));return;}
                B.bNeedsRecovery=false;B.bReady=true;
            }
        }
        else if(R.Code==EAetherDeliveryPumpCode::StorageUnavailable)B.NextDelivery=Now+1;
        else if(R.Code!=EAetherDeliveryPumpCode::Pending)G->Fault(TEXT("Resource delivery failed; persisted record retained"));
    }
    void Reply(FBinding& B,const FAetherCommandResult& Result)
    {
        if(!Current(B))return;
        FAetherV10ReplyPacket Packet;Packet.Channel=B.Channel;FString Reason;
        if(AetherCommands::EncodeResult(Result,Packet.Bytes,Reason))B.Controller->ClientV10Reply(Packet);
    }
};
UAetherCommandRuntime::UAetherCommandRuntime()=default;
UAetherCommandRuntime::~UAetherCommandRuntime()=default;
bool UAetherCommandRuntime::InstallBackend(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store,FAetherResolveConnectedContext Resolve,FAetherPublishConnectedState Publish,FString& Reason)
{
    check(IsInGameThread());
    if(Impl||!GetWorld()||GetWorld()->GetNetMode()==NM_Client||!Resolve||!Publish)
    {Reason=TEXT("Native backend already installed or server context unavailable");return false;}
    const auto& D=FAetherV10Definitions::Get();if(!D.bValid){Reason=D.Error;return false;}
    Impl.Reset(new FAetherCommandRuntimeImpl());Impl->Store=Store;Impl->Resolve=MoveTemp(Resolve);Impl->Publish=MoveTemp(Publish);
    Impl->Coordinator=MakeUnique<FAetherProfileCoordinator>(Store,D.Items,D.Skills,D.Rules,D.Economy,D.Interactions,D.Progression);
    Impl->Facts=MakeUnique<FAetherServerFactCoordinator>(Store);
    Reason.Reset();return true;
}
bool UAetherCommandRuntime::SetBackendDomain(FGuid Realm)
{
    if(!IsInstalled()||!Realm.IsValid()||(!Impl->Bindings.IsEmpty()&&Impl->Realm!=Realm))return false;
    Impl->Realm=Realm;return true;
}
bool UAetherCommandRuntime::IsInstalled() const{return Impl&&Impl->Coordinator&&!Impl->bShutdownRequested;}
void UAetherCommandRuntime::SetContainerAuthorizer(TFunction<bool(AAetherPlayerController&,const FString&,bool)> Authorize)
{if(Impl)Impl->AuthorizeContainer=MoveTemp(Authorize);}
void UAetherCommandRuntime::QueryContainer(AAetherPlayerController* C,const FAetherV10ContainerQuery& Q)
{
    if(!IsInstalled()||Impl->bPolling||!C||!Q.Context.IsValid()||Q.TargetId.Len()>96)return;
    auto* Found=Impl->Bindings.Find(C);if(!Found)return;auto& B=**Found;
    if(Q.Channel!=B.Channel||!Impl->Current(B)||!B.Syncs.Consume(FPlatformTime::Seconds(),Q.TargetId.Len(),2,4))return;
    if(Q.TargetId.IsEmpty()){if(Q.Context==B.ContainerContext)Impl->CloseContainer(B);return;}
    if(!B.bReady||!Impl->AuthorizeContainer)return;
    if(Q.Context!=B.ContainerContext||!Q.TargetId.Equals(B.ContainerId,ESearchCase::CaseSensitive))
    {
        Impl->CloseContainer(B);
        if(!Impl->AuthorizeContainer(*C,Q.TargetId,true)){C->ClientV10ContainerClosed(B.Channel,Q.Context);return;}
        B.ContainerContext=Q.Context;B.ContainerId=Q.TargetId;
    }
    B.bContainerDirty=true;
}
void UAetherCommandRuntime::SetContainerPublisher(TFunction<void(const FAetherContainerStateV10&)> Publisher)
{if(Impl)Impl->PublishContainer=MoveTemp(Publisher);}
void UAetherCommandRuntime::SetWorldPublisher(TFunction<void(const FAetherWorldStateV10&)> Publisher)
{check(IsInGameThread());if(Impl&&!Impl->bPolling&&!Impl->bTicking)Impl->PublishWorld=MoveTemp(Publisher);}
bool UAetherCommandRuntime::HasPendingServerFact(const FString& Character,FName Fact) const
{return IsInstalled()&&Impl->Facts&&(Impl->Facts->HasPendingFact(Character,Fact.ToString())||Impl->DeferredFacts.ContainsByPredicate([&](const auto& E){return E.CharacterId.Equals(Character,ESearchCase::CaseSensitive)&&E.FactId==Fact.ToString();}));}
bool UAetherCommandRuntime::ObserveServerFact(FAetherServerFact Event,FString& Reason)
{
    check(IsInGameThread());
    if(!IsInstalled()||Impl->bPolling||!Impl->Facts){Reason=TEXT("Native server fact service not ready");return false;}
    // 观察时间在进入保留队列时固定，跨 UTC 零点重试不能被重新归入新日。
    if((Event.Kind==EAetherServerFactKind::Daily||Event.Kind==EAetherServerFactKind::EncounterReward)&&Event.UtcDay.IsEmpty())
        Event.UtcDay=FDateTime::UtcNow().ToString(TEXT("%Y%m%d"));
    Impl->PumpFacts();
    if(Impl->Facts->Enqueue(Event,Reason))return true;
    if(!Reason.Contains(TEXT("queue full")))return false;
    if(Event.Kind!=EAetherServerFactKind::EquipmentWear)
        for(const auto& E:Impl->DeferredFacts)if(E.Kind==Event.Kind&&E.CharacterId.Equals(Event.CharacterId,ESearchCase::CaseSensitive)&&
            E.FactId==Event.FactId&&E.SourceId==Event.SourceId&&E.InstanceId==Event.InstanceId&&E.UtcDay==Event.UtcDay){Reason.Reset();return true;}
    if(Impl->DeferredFacts.Num()>=1024){Reason=TEXT("Retained server fact queue exhausted");return false;}
    Impl->DeferredFacts.Add(MoveTemp(Event));Reason.Reset();return true;
}
bool UAetherCommandRuntime::BindVerifiedPlayer(AAetherPlayerController* C,const FString& Character)
{
    check(IsInGameThread());
    if(!IsInstalled()||!Impl->Realm.IsValid()||Impl->bPolling||!C||!C->HasAuthority()||C->GetGameInstance()!=GetGameInstance()||!C->GetPawn())return false;
    auto* PS=C->GetPlayerState<AAetherPlayerState>();
    // 身份来自服务器登录流程和当前 PlayerState，不读取命令里自报的 ActorIdentity。
    // 此方法不等于账号认证；原型 DevProfile 也不能因此被宣称为正式账号服务。
    if(!PS||!PS->Profile.CharacterId.Equals(Character,ESearchCase::CaseSensitive))return false;
    if(const auto* Existing=Impl->Bindings.Find(C);Existing&&Impl->Current(**Existing)&&
        (*Existing)->Session.CharacterId.Equals(Character,ESearchCase::CaseSensitive))return true;
    TArray<AAetherPlayerController*> Previous;
    for(const auto& P:Impl->Bindings)if(P.Value->Session.CharacterId==Character)
    {
        // 大小写别名既不能接管连接，也不能在拒绝之前踢掉原角色。
        if(!P.Value->Session.CharacterId.Equals(Character,ESearchCase::CaseSensitive))return false;
        Previous.Add(P.Key.Get());
    }
    for(auto* Old:Previous)UnbindPlayer(Old);
    UnbindPlayer(C);if(Impl->Bindings.Num()>=16)return false;
    const auto Session=Impl->Coordinator->BeginSession(Character);if(!Session.SessionId.IsValid())return false;
    auto B=MakeUnique<FAetherCommandRuntimeImpl::FBinding>();B->Controller=C;B->PlayerState=PS;B->Pawn=C->GetPawn();B->Session=Session;B->Channel=FGuid::NewGuid();
    FAetherServerFact Settle;Settle.Kind=EAetherServerFactKind::Settle;Settle.CharacterId=Character;FString Why;
    if(!ObserveServerFact(MoveTemp(Settle),Why)){Impl->Coordinator->EndSession(Session);return false;}
    if(auto* Resources=Impl->Gate(*B))Resources->BlockForInitialLoad();
    B->Read=Impl->Store->Read({EAetherAggregateKind::Profile,Character});
    const auto Channel=B->Channel;Impl->Bindings.Add(C,MoveTemp(B));C->ClientV10Channel(Channel,Character,Impl->Realm);return true;
}
void UAetherCommandRuntime::UnbindPlayer(AAetherPlayerController* C)
{
    if(!IsInstalled()||Impl->bPolling||!C)return;
    if(auto* B=Impl->Bindings.Find(C))
    {Impl->CloseContainer(**B);Impl->Coordinator->EndSession((*B)->Session);Impl->Bindings.Remove(C);if(IsValid(C))C->ClientV10Channel({},{},{});}
}
void UAetherCommandRuntime::NotifyPawnChanged(AAetherPlayerController* C)
{
    if(!IsInstalled()||Impl->bPolling||!C)return;
    auto* Found=Impl->Bindings.Find(C);if(!Found)return;auto& B=**Found;
    if(B.Pawn.Get()==C->GetPawn()&&B.PlayerState.Get()==C->GetPlayerState<AAetherPlayerState>())return;
    if(B.PlayerState.Get()!=C->GetPlayerState<AAetherPlayerState>()){UnbindPlayer(C);return;}
    Impl->CloseContainer(B);
    B.Session=Impl->Coordinator->ReplacePawn(B.Session);
    B.Pawn=C->GetPawn();B.Channel=FGuid::NewGuid();B.SceneSequence=0;B.bReady=false;B.Outgoing.Reset();B.Read={};B.Revision=-1;B.InFlightTransfer.Invalidate();
    B.bNeedsRecovery=true;B.bNeedFullRecovery=true;B.bDeliveryRequested=false;B.Delivery.Reset();
    B.ResourceCommand.Reset();B.Proof={};B.ProofRead={};B.NextDelivery=0;
    if(!B.Session.SessionId.IsValid()){UnbindPlayer(C);return;}
    C->ClientV10Channel(B.Channel,B.Session.CharacterId,Impl->Realm);
    if(B.Pawn.IsValid()){if(auto* Resources=Impl->Gate(B))Resources->BlockForInitialLoad();B.Read=Impl->Store->Read({EAetherAggregateKind::Profile,B.Session.CharacterId});}
    // 不重置连接令牌桶，频繁重生不能绕过限流。
}
bool UAetherCommandRuntime::AuthorizeSceneInput(AAetherPlayerController* C,const FAetherV10CommandPacket& Packet,uint64 Sequence,FAetherPlayerCommand& Command)
{
    if(!IsInstalled()||Impl->bPolling||!C)return false;
    auto* Found=Impl->Bindings.Find(C);if(!Found)return false;auto& B=**Found;
    if(!B.bReady||Packet.Channel!=B.Channel||!Impl->Current(B)||Sequence<=B.SceneSequence||
        !B.Requests.Consume(FPlatformTime::Seconds(),Packet.Bytes.Num()))return false;
    FString Why;if(!AetherCommands::Decode(Packet.Bytes,Command,Why)||Command.Type!=EAetherCommandType::ExecuteInteraction||
        Command.ProtocolVersion!=AetherCommands::LatestProtocolVersion)return false;
    // 消耗序号后即使条件失败也不自动重做；下一次操作必须来自新的明确输入。
    B.SceneSequence=Sequence;return true;
}
void UAetherCommandRuntime::Receive(AAetherPlayerController* C,const FAetherV10CommandPacket& Packet)
{
    if(!IsInstalled()||Impl->bPolling||!C)return;
    auto* Found=Impl->Bindings.Find(C);if(!Found)return;auto& B=**Found;
    if(Packet.Channel!=B.Channel||!Impl->Current(B)||!B.Requests.Consume(FPlatformTime::Seconds(),Packet.Bytes.Num()))return;
    FAetherPlayerCommand Command;FString Reason;
    if(!AetherCommands::Decode(Packet.Bytes,Command,Reason))return;
    FAetherCommandResult Rejection;Rejection.CommandId=Command.CommandId;
    // 协调者按同一个完整请求查持久回执，重试不会在这里换 ID、价格或当前版本。
    if(!Impl->Coordinator->Submit(B.Session,Command,Rejection))Impl->Reply(B,Rejection);
}
void UAetherCommandRuntime::RequestSnapshot(AAetherPlayerController* C,FGuid Channel)
{
    if(!IsInstalled()||Impl->bPolling||!C)return;
    auto* Found=Impl->Bindings.Find(C);if(!Found)return;auto& B=**Found;
    if(Channel!=B.Channel||!Impl->Current(B)||!B.Syncs.Consume(FPlatformTime::Seconds(),0,1,2)||B.Read.IsValid())return;
    B.Read=Impl->Store->Read({EAetherAggregateKind::Profile,B.Session.CharacterId});
}
void UAetherCommandRuntime::AcknowledgeSnapshot(AAetherPlayerController* C,FGuid Channel,FGuid Transfer,uint32 NextOffset)
{
    if(!IsInstalled()||Impl->bPolling||!C)return;
    auto* Found=Impl->Bindings.Find(C);if(!Found)return;auto& B=**Found;
    if(!Impl->Current(B)||Channel!=B.Channel||!Transfer.IsValid()||Transfer!=B.InFlightTransfer||NextOffset!=B.InFlightEnd)return;
    B.InFlightTransfer.Invalidate();
}
void UAetherCommandRuntime::Tick(float Dt)
{
    if(Impl&&Impl->bShutdownRequested)
    {
        if(Impl->bPolling||Impl->bTicking)return;
        UninstallBackend();if(!Impl)return;
        // 地图退出停止新输入，但值对象事务仍排空；不再向旧 Pawn 发布状态。
        Impl->PumpFacts();
        Impl->Coordinator->Poll([](const auto&,const auto&,const auto&,auto&){return false;});
        for(const auto& Fact:Impl->Facts->Poll(FPlatformTime::Seconds()))
            if(!Fact.Profile.IsSet())UE_LOG(LogTemp,Error,TEXT("AETHER_SHUTDOWN_FACT_FAILED %s"),*Fact.Detail);
        if(Impl->DeferredFacts.IsEmpty()&&Impl->Facts->PendingCount()==0&&Impl->Coordinator->PendingCount()==0)UninstallBackend();
        return;
    }
    if(!IsInstalled()||Impl->bTicking)return;
    TGuardValue<bool> TickGuard(Impl->bTicking,true);
    TArray<TWeakObjectPtr<AAetherPlayerController>> Keys;Impl->Bindings.GenerateKeyArray(Keys);
    for(const auto& Key:Keys)
    {
        auto* Found=Impl->Bindings.Find(Key);if(!Found)continue;
        if(!Key.IsValid()){Impl->Coordinator->EndSession((*Found)->Session);Impl->Bindings.Remove(Key);continue;}
        NotifyPawnChanged(Key.Get());
    }
    const auto& D=FAetherV10Definitions::Get();
    for(auto& Pair:Impl->Bindings)
    {
        auto& B=*Pair.Value;if(!Impl->Current(B)||!B.Read.IsValid()||!B.Read.IsReady()||B.ResourceCommand.IsSet()||(Impl->Gate(B)&&Impl->Gate(B)->Reservation().IsValid())||(Impl->Coordinator->HasPendingForCharacter(B.Session.CharacterId)||Impl->HasPendingFacts(B.Session.CharacterId)))continue;
        const auto Read=B.Read.Get();B.Read={};FAetherProfileStateV10 Profile;FString Reason;
        if(Read.Code==EAetherStoreCode::Found&&Read.Value.IsSet()&&Read.Value->SchemaVersion==10&&
            AetherProfileCodec::Decode(Read.Value->Payload,D.Items,D.Skills,D.Rules,Profile,Reason)&&Read.Value->Revision==Profile.Revision)
            Impl->Queue(B,Profile);
        // Missing/Corrupt 不创建空档、不从旧内存覆盖磁盘；保留未就绪并允许显式重同步。
    }
    if(Impl->bShutdownRequested)return;
    TArray<FAetherProfileCompletion> Done;
    {
        TGuardValue<bool> Guard(Impl->bPolling,true);
        Done=Impl->Coordinator->Poll([&](const FAetherProfileSession& Session,const FAetherPlayerCommand& Command,const FAetherProfileStateV10& P,FAetherProfileCommandContext& Out)
        {
            for(const auto& Pair:Impl->Bindings)
            {
                auto& B=*Pair.Value;if(!(B.Session==Session)||!Impl->Current(B))continue;
                // 未就绪只阻止新候选；Submit 之前不拦截，已提交请求仍能先查持久回执。
                if(!B.bReady)return false;
                auto* G=Impl->Gate(B);if(!G||G->IsBlocked())return false;
                if(!Impl->Resolve(*B.Controller.Get(),Command,P,Out)||!Impl->Current(B))return false;
                if(Command.Type==EAetherCommandType::UseItem)
                {
                    if(!G->Reserve(Command.CommandId,Out.Resources))return false;
                    Out.ResourceReservationId=Command.CommandId;B.ResourceCommand=Command;
                    const auto Now=FDateTime::UtcNow();Out.ServerUnixMs=Now.ToUnixTimestamp()*1000+Now.GetMillisecond();
                }
                return Impl->Current(B);
            }
            return false;
        });
    }
    for(const auto& Completion:Done)
    {
        if(Completion.WorldSnapshot.IsSet()&&Impl->PublishWorld)Impl->PublishWorld(Completion.WorldSnapshot.GetValue());
        if(Impl->bShutdownRequested)return;
        if(Completion.ContainerSnapshot.IsSet())
        {
            if(Impl->PublishContainer)Impl->PublishContainer(Completion.ContainerSnapshot.GetValue());
            for(auto& Pair:Impl->Bindings)if(Pair.Value->ContainerId.Equals(Completion.ContainerSnapshot->ContainerId,ESearchCase::CaseSensitive))
                Pair.Value->bContainerDirty=true;
        }
        if(Impl->bShutdownRequested)return;
        if(!Completion.bMayPublish||!Impl->Coordinator->IsCurrent(Completion.Session))continue;
        for(auto& Pair:Impl->Bindings)
        {
            auto& B=*Pair.Value;if(!(B.Session==Completion.Session)||!Impl->Current(B))continue;
            Impl->FinishResourceCommand(B,Completion);
            // 必须先由服务器发布已提交事实，再发送拥有者回执/快照；绝不发送 World 中的他人私有聚合。
            if(Completion.Snapshot.IsSet())Impl->Queue(B,Completion.Snapshot.GetValue(),Completion.WorldSnapshot.IsSet()?&Completion.WorldSnapshot.GetValue():nullptr,Completion.ContainerSnapshot.IsSet()?&Completion.ContainerSnapshot.GetValue():nullptr);
            Impl->Reply(B,Completion.Result);break;
        }
    }
    Impl->PumpFacts();
    const auto Facts=Impl->Facts->Poll(FPlatformTime::Seconds());
    for(const auto& Fact:Facts)
    {
        if(Fact.World.IsSet()&&Impl->PublishWorld)Impl->PublishWorld(Fact.World.GetValue());
        if(Impl->bShutdownRequested)return;
        if(!Fact.Profile.IsSet())
        {
            UE_LOG(LogTemp,Warning,TEXT("AETHER_NATIVE_FACT_REJECTED fact=%s code=%d detail=%s"),*Fact.Event.FactId,int32(Fact.Code),*Fact.Detail);
            if(Fact.Event.Kind==EAetherServerFactKind::EquipmentWear)
                for(auto& Pair:Impl->Bindings)
                    if(Pair.Value->Session.CharacterId.Equals(Fact.Event.CharacterId,ESearchCase::CaseSensitive))
                        if(auto* Gate=Impl->Gate(*Pair.Value))Gate->Fault(TEXT("Equipment wear transaction failed"));
            continue;
        }
        for(auto& Pair:Impl->Bindings)
        {
            auto& B=*Pair.Value;
            if(!B.Session.CharacterId.Equals(Fact.Event.CharacterId,ESearchCase::CaseSensitive)||!Impl->Current(B))continue;
            if(B.ResourceCommand.IsSet()||(Impl->Gate(B)&&Impl->Gate(B)->Reservation().IsValid())||
                Impl->Coordinator->HasPendingForCharacter(B.Session.CharacterId)||
                (B.bNeedsRecovery&&Impl->HasPendingFacts(B.Session.CharacterId)))
                B.Read=Impl->Store->Read({EAetherAggregateKind::Profile,B.Session.CharacterId});
            else Impl->Queue(B,Fact.Profile.GetValue(),Fact.World.IsSet()?&Fact.World.GetValue():nullptr);
        }
        if(Fact.Event.Kind==EAetherServerFactKind::World)
            for(const auto& Pair:Impl->Bindings)if(!Pair.Value->Session.CharacterId.Equals(Fact.Event.CharacterId,ESearchCase::CaseSensitive))
            {
                FAetherServerFact Settle;Settle.Kind=EAetherServerFactKind::Settle;Settle.CharacterId=Pair.Value->Session.CharacterId;FString Why;
                if(!ObserveServerFact(MoveTemp(Settle),Why))UE_LOG(LogTemp,Warning,TEXT("AETHER_NATIVE_FACT_SETTLE_DEFERRED %s"),*Why);
            }
    }
    {
        TGuardValue<bool> Guard(Impl->bPolling,true);
        for(auto& Pair:Impl->Bindings)Impl->PollResources(*Pair.Value);
    }
    for(auto& Pair:Impl->Bindings)Impl->PollContainer(*Pair.Value);
    Impl->SendElapsed+=FMath::Clamp(Dt,0.f,.1f);if(Impl->SendElapsed<1.f/30.f)return;Impl->SendElapsed=0;
    Keys.Reset();Impl->Bindings.GenerateKeyArray(Keys);if(Keys.IsEmpty())return;
    int32 Sent=0;
    for(int32 I=0;I<Keys.Num()&&Sent<4;++I)
    {
        Impl->NextSender%=Keys.Num();auto* Found=Impl->Bindings.Find(Keys[Impl->NextSender++]);if(!Found)continue;
        auto& B=**Found;if(!Impl->Current(B)||B.InFlightTransfer.IsValid())continue;
        const bool Container=!B.ContainerOutgoing.IsEmpty()&&(B.Outgoing.IsEmpty()||B.bPreferContainer);
        auto& Bytes=Container?B.ContainerOutgoing:B.Outgoing;if(Bytes.IsEmpty())continue;
        auto& Offset=Container?B.ContainerOffset:B.Offset;
        FAetherV10SnapshotChunk Chunk;Chunk.Channel=B.Channel;Chunk.Transfer=Container?B.ContainerTransfer:B.Transfer;
        Chunk.Revision=Container?B.ContainerRevision:B.Revision;
        if(Container){Chunk.Kind=1;Chunk.Context=B.ContainerContext;Chunk.WorldRevision=B.ContainerWorldRevision;}
        Chunk.Total=Bytes.Num();Chunk.Offset=Offset;Chunk.Checksum=Container?B.ContainerChecksum:B.Checksum;
        const int32 Size=FMath::Min(AetherV10Network::ChunkBytes,Bytes.Num()-int32(Offset));
        Chunk.Bytes.Append(Bytes.GetData()+Offset,Size);Offset+=Size;
        if(Offset==uint32(Bytes.Num()))Bytes.Reset();
        B.bPreferContainer=!Container;
        // 每连接最多一个未确认分片，避免慢连接积累可靠 RPC 直到 reliable buffer overflow。
        // 新快照可以替换 Outgoing，但必须先等旧分片确认，不能靠连续提交越过背压。
        B.InFlightTransfer=Chunk.Transfer;B.InFlightEnd=Chunk.Offset+Chunk.Bytes.Num();
        B.Controller->ClientV10Snapshot(Chunk);++Sent;
    }
}
bool UAetherCommandRuntime::IsTickable() const{return !IsTemplate()&&Impl&&(IsInstalled()||Impl->bShutdownRequested);}
TStatId UAetherCommandRuntime::GetStatId() const{RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherCommandRuntime,STATGROUP_Tickables);}
UWorld* UAetherCommandRuntime::GetTickableGameObjectWorld() const{return GetWorld();}
void UAetherCommandRuntime::UninstallBackend()
{
    check(IsInGameThread());
    if(Impl&&(Impl->bPolling||Impl->bTicking)){Impl->bShutdownRequested=true;return;}
    if(Impl&&(Impl->Facts->PendingCount()>0||Impl->Coordinator->PendingCount()>0||!Impl->DeferredFacts.IsEmpty()))
    {
        if(!Impl->Bindings.IsEmpty()||!Impl->bShutdownRequested)
        {
            Impl->bShutdownRequested=true;
            for(auto& Pair:Impl->Bindings)
            {Impl->Coordinator->EndSession(Pair.Value->Session);if(auto* C=Pair.Key.Get())C->ClientV10Channel({},{},{});}
            Impl->Bindings.Reset();
        }
        return;
    }
    // 先摘除根指针，Client 通知或 UObject 委托重入退出时不会重复遍历正在释放的绑定。
    auto Previous=MoveTemp(Impl);
    if(Previous)
    {
        for(auto& Pair:Previous->Bindings)if(auto* C=Pair.Key.Get())C->ClientV10Channel({},{},{});
        Previous->Bindings.Reset();Previous->Coordinator.Reset();Previous->Facts.Reset();if(Previous->Store)Previous->Store->Close();
    }
}
bool UAetherCommandRuntime::HasBackend() const{return Impl.IsValid();}
bool UAetherCommandRuntime::DrainBackend(double Seconds)
{
    check(IsInGameThread());UninstallBackend();
    // 回调中退出不能嵌套 Poll，也不能提前关 SQLite；留给下一帧收尾。
    if(Impl&&(Impl->bPolling||Impl->bTicking))return false;
    const double Deadline=FPlatformTime::Seconds()+FMath::Clamp(Seconds,0.,5.);
    while(Impl&&FPlatformTime::Seconds()<Deadline){Tick(0);if(Impl)FPlatformProcess::Sleep(.001f);}
    return !Impl;
}
void UAetherCommandRuntime::Deinitialize()
{
    DrainBackend();
    if(Impl)
    {
        UE_LOG(LogTemp,Error,TEXT("AETHER_SHUTDOWN_INCOMPLETE pending_facts=%d deferred=%d commands=%d"),
            Impl->Facts->PendingCount(),Impl->DeferredFacts.Num(),Impl->Coordinator->PendingCount());
        Impl->Store->Close();Impl.Reset();
    }
    Super::Deinitialize();
}

void FAetherCommandRuntimeImplDeleter::operator()(FAetherCommandRuntimeImpl* Value) const { delete Value; }
