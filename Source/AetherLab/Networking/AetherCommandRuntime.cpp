#include "Networking/AetherCommandRuntime.h"
#include "Framework/AetherPlayerController.h"
#include "AetherProgression.h"
#include "Definitions/AetherV10Definitions.h"
#include "Profile/AetherProfileCodec.h"
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
        bool bReady=false;
    };
    TSharedPtr<IAetherTransactionalStore,ESPMode::ThreadSafe> Store;
    TUniquePtr<FAetherProfileCoordinator> Coordinator;
    FAetherResolveConnectedContext Resolve;
    FAetherPublishConnectedState Publish;
    TMap<TWeakObjectPtr<AAetherPlayerController>,TUniquePtr<FBinding>> Bindings;
    int32 NextSender=0;
    float SendElapsed=0;
    bool bPolling=false;
    bool Current(const FBinding& B) const
    {
        const auto* C=B.Controller.Get();const auto* PS=B.PlayerState.Get();
        return C&&PS&&!C->IsActorBeingDestroyed()&&!PS->IsActorBeingDestroyed()&&C->HasAuthority()&&C->GetPlayerState<AAetherPlayerState>()==PS&&C->GetPawn()==B.Pawn.Get()&&B.Pawn.IsValid()&&!B.Pawn->IsActorBeingDestroyed()&&
            PS->Profile.CharacterId.Equals(B.Session.CharacterId,ESearchCase::CaseSensitive)&&Coordinator&&Coordinator->IsCurrent(B.Session);
    }
    void Queue(FBinding& B,const FAetherProfileStateV10& P,const FAetherWorldStateV10* World=nullptr,const FAetherContainerStateV10* Container=nullptr)
    {
        if(!Current(B)||!P.CharacterId.Equals(B.Session.CharacterId,ESearchCase::CaseSensitive)||P.Revision<B.Revision)return;
        // 先发布持久事实，失败保持未就绪，不能让界面领先于角色实际能力。
        B.bReady=false;B.Outgoing.Reset();B.Revision=P.Revision;
        {TGuardValue<bool> Guard(bPolling,true);if(!Publish(*B.Controller.Get(),P,World,Container)||!Current(B))return;}
        const auto& D=FAetherV10Definitions::Get();FString Reason;TArray<uint8> Bytes;
        if(!AetherProfileCodec::Encode(P,D.Items,D.Skills,D.Rules,Bytes,Reason))return;
        // 同一连接最多一份待发送快照，新提交替换尚未发完的旧快照；客户端用 Transfer 区分。
        B.Outgoing=MoveTemp(Bytes);B.Transfer=FGuid::NewGuid();B.Revision=P.Revision;B.Offset=0;
        B.Checksum=FCrc::MemCrc32(B.Outgoing.GetData(),B.Outgoing.Num());B.bReady=true;
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
    if(IsInstalled()||!GetWorld()||GetWorld()->GetNetMode()==NM_Client||!Resolve||!Publish)
    {Reason=TEXT("Native backend already installed or server context unavailable");return false;}
    const auto& D=FAetherV10Definitions::Get();if(!D.bValid){Reason=D.Error;return false;}
    Impl=MakeUnique<FAetherCommandRuntimeImpl>();Impl->Store=Store;Impl->Resolve=MoveTemp(Resolve);Impl->Publish=MoveTemp(Publish);
    Impl->Coordinator=MakeUnique<FAetherProfileCoordinator>(Store,D.Items,D.Skills,D.Rules,D.Economy,D.Interactions,D.Progression);
    Reason.Reset();return true;
}
bool UAetherCommandRuntime::IsInstalled() const{return Impl&&Impl->Coordinator;}
bool UAetherCommandRuntime::BindVerifiedPlayer(AAetherPlayerController* C,const FString& Character)
{
    check(IsInGameThread());
    if(!IsInstalled()||Impl->bPolling||!C||!C->HasAuthority()||C->GetGameInstance()!=GetGameInstance()||!C->GetPawn())return false;
    auto* PS=C->GetPlayerState<AAetherPlayerState>();
    // 身份来自服务器登录流程和当前 PlayerState，不读取命令里自报的 ActorIdentity。
    // 此方法不等于账号认证；原型 DevProfile 也不能因此被宣称为正式账号服务。
    if(!PS||!PS->Profile.CharacterId.Equals(Character,ESearchCase::CaseSensitive))return false;
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
    if(!B.Session.SessionId.IsValid()){UnbindPlayer(C);return;}
    C->ClientV10Channel(B.Channel,B.Session.CharacterId);
    if(B.Pawn.IsValid())B.Read=Impl->Store->Read({EAetherAggregateKind::Profile,B.Session.CharacterId});
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
    if(!IsInstalled())return;
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
        auto& B=*Pair.Value;if(!Impl->Current(B)||!B.Read.IsValid()||!B.Read.IsReady())continue;
        const auto Read=B.Read.Get();B.Read={};FAetherProfileStateV10 Profile;FString Reason;
        if(Read.Code==EAetherStoreCode::Found&&Read.Value.IsSet()&&Read.Value->SchemaVersion==10&&
            AetherProfileCodec::Decode(Read.Value->Payload,D.Items,D.Skills,D.Rules,Profile,Reason)&&Read.Value->Revision==Profile.Revision)
            Impl->Queue(B,Profile);
        // Missing/Corrupt 不创建空档、不从旧内存覆盖磁盘；保留未就绪并允许显式重同步。
    }
    TArray<FAetherProfileCompletion> Done;
    {
        TGuardValue<bool> Guard(Impl->bPolling,true);
        Done=Impl->Coordinator->Poll([&](const FAetherProfileSession& Session,const FAetherPlayerCommand& Command,const FAetherProfileStateV10& P,FAetherProfileCommandContext& Out)
        {
            for(const auto& Pair:Impl->Bindings)
            {
                const auto& B=*Pair.Value;if(!(B.Session==Session)||!Impl->Current(B))continue;
                // 未就绪只阻止新候选；Submit 之前不拦截，已提交请求仍能先查持久回执。
                if(!B.bReady)return false;
                const bool Allowed=Impl->Resolve(*B.Controller.Get(),Command,P,Out);
                return Allowed&&Impl->Current(B);
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
            // 必须先由服务器发布已提交事实，再发送拥有者回执/快照；绝不发送 World 中的他人私有聚合。
            if(Completion.Snapshot.IsSet())Impl->Queue(B,Completion.Snapshot.GetValue(),Completion.WorldSnapshot.IsSet()?&Completion.WorldSnapshot.GetValue():nullptr,Completion.ContainerSnapshot.IsSet()?&Completion.ContainerSnapshot.GetValue():nullptr);
            Impl->Reply(B,Completion.Result);break;
        }
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
bool UAetherCommandRuntime::IsTickable() const{return !IsTemplate()&&IsInstalled();}
TStatId UAetherCommandRuntime::GetStatId() const{RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherCommandRuntime,STATGROUP_Tickables);}
UWorld* UAetherCommandRuntime::GetTickableGameObjectWorld() const{return GetWorld();}
void UAetherCommandRuntime::Deinitialize()
{
    if(Impl)
    {
        for(auto& Pair:Impl->Bindings)if(auto* C=Pair.Key.Get())C->ClientV10Channel({},{});
        Impl->Bindings.Reset();Impl->Coordinator.Reset();if(Impl->Store)Impl->Store->Close();Impl.Reset();
    }
    Super::Deinitialize();
}
