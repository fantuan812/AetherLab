#include "Networking/AetherCommandRuntime.h"
#include "Framework/AetherPlayerController.h"
#include "AetherProgression.h"
#include "Definitions/AetherV10Definitions.h"
#include "Profile/AetherProfileCodec.h"
#include "Inventory/AetherResourceGate.h"
#include "Commands/AetherConsumableDeliveryPump.h"
#include "Misc/DateTime.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
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
    };
    TSharedPtr<IAetherTransactionalStore,ESPMode::ThreadSafe> Store;
    TUniquePtr<FAetherProfileCoordinator> Coordinator;
    TUniquePtr<FAetherServerFactCoordinator> Facts;
    FAetherResolveConnectedContext Resolve;
    FAetherPublishConnectedState Publish;
    TMap<TWeakObjectPtr<AAetherPlayerController>,TUniquePtr<FBinding>> Bindings;
    int32 NextSender=0;
    float SendElapsed=0;
    bool bPolling=false,bTicking=false,bShutdownRequested=false;
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
    if((Impl&&(Impl->bPolling||Impl->bTicking))||IsInstalled()||!GetWorld()||GetWorld()->GetNetMode()==NM_Client||!Resolve||!Publish)
    {Reason=TEXT("Native backend already installed or server context unavailable");return false;}
    const auto& D=FAetherV10Definitions::Get();if(!D.bValid){Reason=D.Error;return false;}
    Impl=MakeUnique<FAetherCommandRuntimeImpl>();Impl->Store=Store;Impl->Resolve=MoveTemp(Resolve);Impl->Publish=MoveTemp(Publish);
    Impl->Coordinator=MakeUnique<FAetherProfileCoordinator>(Store,D.Items,D.Skills,D.Rules,D.Economy,D.Interactions,D.Progression);
    Impl->Facts=MakeUnique<FAetherServerFactCoordinator>(Store);
    Reason.Reset();return true;
}
bool UAetherCommandRuntime::IsInstalled() const{return Impl&&Impl->Coordinator&&!Impl->bShutdownRequested;}
bool UAetherCommandRuntime::HasPendingServerFact(const FString& Character,FName Fact) const
{return IsInstalled()&&Impl->Facts&&Impl->Facts->HasPendingFact(Character,Fact.ToString());}
bool UAetherCommandRuntime::ObserveServerFact(FAetherServerFact Event,FString& Reason)
{
    check(IsInGameThread());
    if(!IsInstalled()||Impl->bPolling||!Impl->Facts){Reason=TEXT("Native server fact service not ready");return false;}
    return Impl->Facts->Enqueue(MoveTemp(Event),Reason);
}
bool UAetherCommandRuntime::BindVerifiedPlayer(AAetherPlayerController* C,const FString& Character)
{
    check(IsInGameThread());
    if(!IsInstalled()||Impl->bPolling||!C||!C->HasAuthority()||C->GetGameInstance()!=GetGameInstance()||!C->GetPawn())return false;
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
    if(!Impl->Facts->Enqueue(MoveTemp(Settle),Why)){Impl->Coordinator->EndSession(Session);return false;}
    if(auto* Resources=Impl->Gate(*B))Resources->BlockForInitialLoad();
    B->Read=Impl->Store->Read({EAetherAggregateKind::Profile,Character});
    const auto Channel=B->Channel;Impl->Bindings.Add(C,MoveTemp(B));C->ClientV10Channel(Channel,Character);return true;
}
void UAetherCommandRuntime::UnbindPlayer(AAetherPlayerController* C)
{
    if(!IsInstalled()||Impl->bPolling||!C)return;
    if(auto* B=Impl->Bindings.Find(C))
    {Impl->Coordinator->EndSession((*B)->Session);Impl->Bindings.Remove(C);if(IsValid(C))C->ClientV10Channel({},{});}
}
void UAetherCommandRuntime::NotifyPawnChanged(AAetherPlayerController* C)
{
    if(!IsInstalled()||Impl->bPolling||!C)return;
    auto* Found=Impl->Bindings.Find(C);if(!Found)return;auto& B=**Found;
    if(B.Pawn.Get()==C->GetPawn()&&B.PlayerState.Get()==C->GetPlayerState<AAetherPlayerState>())return;
    if(B.PlayerState.Get()!=C->GetPlayerState<AAetherPlayerState>()){UnbindPlayer(C);return;}
    B.Session=Impl->Coordinator->ReplacePawn(B.Session);
    B.Pawn=C->GetPawn();B.Channel=FGuid::NewGuid();B.bReady=false;B.Outgoing.Reset();B.Read={};B.Revision=-1;B.InFlightTransfer.Invalidate();
    B.bNeedsRecovery=true;B.bNeedFullRecovery=true;B.bDeliveryRequested=false;B.Delivery.Reset();
    B.ResourceCommand.Reset();B.Proof={};B.ProofRead={};B.NextDelivery=0;
    if(!B.Session.SessionId.IsValid()){UnbindPlayer(C);return;}
    C->ClientV10Channel(B.Channel,B.Session.CharacterId);
    if(B.Pawn.IsValid()){if(auto* Resources=Impl->Gate(B))Resources->BlockForInitialLoad();B.Read=Impl->Store->Read({EAetherAggregateKind::Profile,B.Session.CharacterId});}
    // 不重置连接令牌桶，频繁重生不能绕过限流。
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
    if(Impl&&Impl->bShutdownRequested){UninstallBackend();return;}
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
        auto& B=*Pair.Value;if(!Impl->Current(B)||!B.Read.IsValid()||!B.Read.IsReady()||(Impl->Coordinator->HasPendingForCharacter(B.Session.CharacterId)||Impl->Facts->HasPendingForCharacter(B.Session.CharacterId)))continue;
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
    const auto Facts=Impl->Facts->Poll(FPlatformTime::Seconds());
    for(const auto& Fact:Facts)
    {
        if(!Fact.Profile.IsSet())
        {UE_LOG(LogTemp,Warning,TEXT("AETHER_NATIVE_FACT_REJECTED fact=%s code=%d detail=%s"),*Fact.Event.FactId,int32(Fact.Code),*Fact.Detail);continue;}
        for(auto& Pair:Impl->Bindings)
        {
            auto& B=*Pair.Value;
            if(!B.Session.CharacterId.Equals(Fact.Event.CharacterId,ESearchCase::CaseSensitive)||!Impl->Current(B))continue;
            if(Impl->Coordinator->HasPendingForCharacter(B.Session.CharacterId)||
                (B.bNeedsRecovery&&Impl->Facts->HasPendingForCharacter(B.Session.CharacterId)))
                B.Read=Impl->Store->Read({EAetherAggregateKind::Profile,B.Session.CharacterId});
            else Impl->Queue(B,Fact.Profile.GetValue(),Fact.World.IsSet()?&Fact.World.GetValue():nullptr);
        }
        if(Fact.Event.Kind==EAetherServerFactKind::World)
            for(const auto& Pair:Impl->Bindings)if(!Pair.Value->Session.CharacterId.Equals(Fact.Event.CharacterId,ESearchCase::CaseSensitive))
            {
                FAetherServerFact Settle;Settle.Kind=EAetherServerFactKind::Settle;Settle.CharacterId=Pair.Value->Session.CharacterId;FString Why;
                if(!Impl->Facts->Enqueue(MoveTemp(Settle),Why))UE_LOG(LogTemp,Warning,TEXT("AETHER_NATIVE_FACT_SETTLE_DEFERRED %s"),*Why);
            }
    }
    {
        TGuardValue<bool> Guard(Impl->bPolling,true);
        for(auto& Pair:Impl->Bindings)Impl->PollResources(*Pair.Value);
    }
    Impl->SendElapsed+=FMath::Clamp(Dt,0.f,.1f);if(Impl->SendElapsed<1.f/30.f)return;Impl->SendElapsed=0;
    Keys.Reset();Impl->Bindings.GenerateKeyArray(Keys);if(Keys.IsEmpty())return;
    int32 Sent=0;
    for(int32 I=0;I<Keys.Num()&&Sent<4;++I)
    {
        Impl->NextSender%=Keys.Num();auto* Found=Impl->Bindings.Find(Keys[Impl->NextSender++]);if(!Found)continue;
        auto& B=**Found;if(!Impl->Current(B)||B.Outgoing.IsEmpty()||B.InFlightTransfer.IsValid())continue;
        FAetherV10SnapshotChunk Chunk;Chunk.Channel=B.Channel;Chunk.Transfer=B.Transfer;Chunk.Revision=B.Revision;
        Chunk.Total=B.Outgoing.Num();Chunk.Offset=B.Offset;Chunk.Checksum=B.Checksum;
        const int32 Size=FMath::Min(AetherV10Network::ChunkBytes,B.Outgoing.Num()-int32(B.Offset));
        Chunk.Bytes.Append(B.Outgoing.GetData()+B.Offset,Size);B.Offset+=Size;
        if(B.Offset==uint32(B.Outgoing.Num()))B.Outgoing.Reset();
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
    // 先摘除根指针，Client 通知或 UObject 委托重入退出时不会重复遍历正在释放的绑定。
    auto Previous=MoveTemp(Impl);
    if(Previous)
    {
        for(auto& Pair:Previous->Bindings)if(auto* C=Pair.Key.Get())C->ClientV10Channel({},{});
        Previous->Bindings.Reset();Previous->Coordinator.Reset();Previous->Facts.Reset();if(Previous->Store)Previous->Store->Close();
    }
}
void UAetherCommandRuntime::Deinitialize(){UninstallBackend();Super::Deinitialize();}
