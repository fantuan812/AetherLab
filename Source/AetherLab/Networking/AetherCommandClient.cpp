#include "Networking/AetherCommandClient.h"
#include "Framework/AetherPlayerController.h"
#include "Definitions/AetherV10Definitions.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherContainerCodec.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"

int32 UAetherCommandClient::PendingIndex() const
{return Pending.IndexOfByPredicate([&](const auto& P){return P.Owner.Equals(Owner,ESearchCase::CaseSensitive);});}
bool UAetherCommandClient::HasPending() const{return PendingIndex()!=INDEX_NONE;}
bool UAetherCommandClient::Matches(AAetherPlayerController* C,FGuid Id) const
{return C&&!C->IsActorBeingDestroyed()&&Controller.Get()==C&&C->GetLocalPlayer()==GetLocalPlayer()&&C->IsLocalController()&&Channel.IsValid()&&Id==Channel;}

void UAetherCommandClient::ReceiveChannel(AAetherPlayerController* C,FGuid Id,const FString& Identity)
{
    if(!C||C->IsActorBeingDestroyed()||!C->IsLocalController()||C->GetLocalPlayer()!=GetLocalPlayer()||GetLocalPlayer()->GetPlayerController(C->GetWorld())!=C)return;
    // 延迟到达的旧 Controller 不能清除后来建立的拥有者通道。
    if(!Id.IsValid()){DetachController(C);return;}
    if(Identity.IsEmpty()||Identity.Len()>32)return;
    for(TCHAR Ch:Identity)if(!FChar::IsAlnum(Ch)&&Ch!='_')return;
    if(Controller.Get()==C&&Channel==Id&&Owner.Equals(Identity,ESearchCase::CaseSensitive))return;
    ResetContainer();Controller=C;Channel=Id;Owner=Identity;Profile.Reset();Assembly={};NextSync=0;
    // Pending 保留原拥有者/授权通道；新通道不会在 Tick 中自动续发旧意图。
    OnChanged.Broadcast();
}
void UAetherCommandClient::DetachController(AAetherPlayerController* C)
{
    if(Controller.Get()!=C)return;
    ResetContainer();Controller.Reset();Channel.Invalidate();Owner.Reset();Profile.Reset();Assembly={};OnChanged.Broadcast();
}
bool UAetherCommandClient::Submit(FGuid ExpectedChannel,const FString& Identity,const TArray<uint8>& Bytes,FString& Reason)
{
    if(!Matches(Controller.Get(),ExpectedChannel)||!Owner.Equals(Identity,ESearchCase::CaseSensitive)||!Profile.IsSet())
    {Reason=TEXT("角色快照尚未就绪或所属会话已改变。");return false;}
    if(HasPending()||Pending.Num()>=8){Reason=TEXT("请先查询或重试尚未完成的命令。");return false;}
    FAetherPlayerCommand Command;
    if(Bytes.IsEmpty()||Bytes.Num()>AetherV10Network::CommandBytes||!AetherCommands::Decode(Bytes,Command,Reason))return false;
    if(Command.ExpectedProfileRevision!=Profile->Revision){Reason=TEXT("角色资料已变化，请重新查看后确认。");return false;}
    // 入队发生在 RPC 前，监听服务器即使同步交回回执也能找到原命令。
    FPending P;P.Owner=Owner;P.Id=Command.CommandId;P.AuthorizedChannel=Channel;P.Bytes=Bytes;
    Pending.Add(MoveTemp(P));Reason.Reset();SendPending();OnChanged.Broadcast();return true;
}
void UAetherCommandClient::SendPending()
{
    const int32 Index=PendingIndex();if(Index==INDEX_NONE||!Profile.IsSet()||!Matches(Controller.Get(),Channel))return;
    auto& P=Pending[Index];const double Now=FPlatformTime::Seconds();
    if(P.AuthorizedChannel!=Channel||P.Receipt.IsSet()||P.Attempts>=5||Now<P.NextAttempt)return;
    // 最多五次，2/4/8/16/16 秒退避。发送的是入队时的字节，不替换版本或数量。
    ++P.Attempts;P.NextAttempt=Now+double(1<<FMath::Min(P.Attempts,4));
    FAetherV10CommandPacket Packet;Packet.Channel=Channel;Packet.Bytes=P.Bytes;
    Controller->ServerV10Command(Packet);
}
bool UAetherCommandClient::RetryPending()
{
    const int32 Index=PendingIndex();
    if(Index==INDEX_NONE||!Profile.IsSet()||!Matches(Controller.Get(),Channel))return false;
    auto& P=Pending[Index];
    if(P.Receipt.IsSet()){RequestSnapshot();return true;}
    // 只允许当前服务器确认的同一精确身份重新授权；不会把别人的挂起请求带进新角色。
    if(FPlatformTime::Seconds()<P.NextAttempt&&P.AuthorizedChannel==Channel)return false;
    P.AuthorizedChannel=Channel;P.Attempts=0;P.NextAttempt=0;SendPending();OnChanged.Broadcast();return true;
}
void UAetherCommandClient::RequestSnapshot()
{
    const double Now=FPlatformTime::Seconds();
    if(!Matches(Controller.Get(),Channel)||Now<NextSync)return;
    NextSync=Now+1;Controller->ServerV10RequestSnapshot(Channel);
}
void UAetherCommandClient::RetirePublished()
{
    const int32 Index=PendingIndex();
    if(Index!=INDEX_NONE&&Profile.IsSet()&&Pending[Index].Receipt.IsSet()&&
        Profile->Revision>=Pending[Index].Receipt->FinalProfileRevision)Pending.RemoveAt(Index);
}
void UAetherCommandClient::ReceiveReply(AAetherPlayerController* C,const FAetherV10ReplyPacket& Packet)
{
    if(!Matches(C,Packet.Channel)||Packet.Bytes.Num()>AetherV10Network::ReplyBytes)return;
    FAetherCommandResult Result;FString Reason;if(!AetherCommands::DecodeResult(Packet.Bytes,Result,Reason))return;
    const int32 Index=PendingIndex();if(Index==INDEX_NONE||Pending[Index].Id!=Result.CommandId)return;
    const bool Success=Result.Code==EAetherCommandCode::Applied||Result.Code==EAetherCommandCode::Replayed;
    // 已收到持久成功后，延迟的重试拒绝不能降级结果或越过快照屏障。
    if(Pending[Index].Receipt.IsSet()&&(!Success||Pending[Index].Receipt->FinalProfileRevision!=Result.FinalProfileRevision))return;
    if(Success)Pending[Index].Receipt=Result;
    else if(Result.Code!=EAetherCommandCode::Busy&&Result.Code!=EAetherCommandCode::StorageUnavailable)Pending.RemoveAt(Index);
    RetirePublished();
    // 始终先更新服务状态再通知控件，避免同步回调提交时看到过期的队列。
    OnResult.Broadcast(Result);OnChanged.Broadcast();
    if(Success||Result.Code==EAetherCommandCode::StaleRevision){RequestSnapshot();RefreshContainer();}
}
void UAetherCommandClient::ReceiveChunk(AAetherPlayerController* C,const FAetherV10SnapshotChunk& P)
{
    if(!Matches(C,P.Channel)||!P.Transfer.IsValid()||P.Revision<0||P.Revision==MAX_int64||
        P.Total==0||P.Total>AetherV10Network::SnapshotBytes||P.Offset>=P.Total||P.Bytes.IsEmpty()||
        P.Bytes.Num()>AetherV10Network::ChunkBytes||uint32(P.Bytes.Num())>P.Total-P.Offset)return;
    // 确认已收到的有界片段；即使内容版本已过期也归还流量额度，防止服务端卡住。
    C->ServerV10SnapshotAck(Channel,P.Transfer,P.Offset+P.Bytes.Num());
    if(P.Kind==1){ReceiveContainerChunk(P);return;}
    if(P.Kind!=0||P.Context.IsValid()||P.WorldRevision!=-1)return;
    if(Profile.IsSet()&&P.Revision<Profile->Revision)return;
    const double Now=FPlatformTime::Seconds();
    if(P.Offset==0)
    {
        if(Assembly.Transfer.IsValid()&&P.Revision<Assembly.Revision)return;
        Assembly={};Assembly.Transfer=P.Transfer;Assembly.Revision=P.Revision;Assembly.Total=P.Total;Assembly.Checksum=P.Checksum;
        Assembly.Bytes.Reserve(int32(P.Total));
    }
    if(Assembly.Transfer!=P.Transfer||Assembly.Revision!=P.Revision||Assembly.Total!=P.Total||Assembly.Checksum!=P.Checksum||
        uint32(Assembly.Bytes.Num())!=P.Offset)return;
    Assembly.LastChunkAt=Now;Assembly.Bytes.Append(P.Bytes);
    if(uint32(Assembly.Bytes.Num())!=Assembly.Total)return;
    const auto& D=FAetherV10Definitions::Get();FAetherProfileStateV10 Candidate;FString Reason;
    // CRC 只检查组装完整性，身份可信度来自拥有者 RPC；校验完整份 DTO 才替换展示。
    const bool Valid=D.bValid&&FCrc::MemCrc32(Assembly.Bytes.GetData(),Assembly.Bytes.Num())==Assembly.Checksum&&
        AetherProfileCodec::Decode(Assembly.Bytes,D.Items,D.Skills,D.Rules,Candidate,Reason)&&
        Candidate.CharacterId.Equals(Owner,ESearchCase::CaseSensitive)&&Candidate.Revision==Assembly.Revision;
    Assembly={};if(!Valid){RequestSnapshot();return;}
    Profile=MoveTemp(Candidate);RetirePublished();OnChanged.Broadcast();
}
void UAetherCommandClient::Tick(float)
{
    if(Assembly.Transfer.IsValid()&&FPlatformTime::Seconds()-Assembly.LastChunkAt>30){Assembly={};RequestSnapshot();}
    if(ContainerContext.IsValid())
    {
        if(GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>()->GetPage()!=EAetherMenuPage::Inventory)CloseContainer();
        else if(FPlatformTime::Seconds()>=NextContainerSync)
        {
            // 空响应和丢失的打开请求同样可恢复；只读刷新不会重放转移意图。
            if(ContainerAssembly.Transfer.IsValid()&&FPlatformTime::Seconds()-ContainerAssembly.LastChunkAt>30)ContainerAssembly={};
            RefreshContainer();
        }
    }
    SendPending();
}
bool UAetherCommandClient::IsTickable() const
{return !IsTemplate()&&Controller.IsValid()&&Channel.IsValid()&&(Assembly.Transfer.IsValid()||ContainerContext.IsValid()||HasPending());}
TStatId UAetherCommandClient::GetStatId() const{RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherCommandClient,STATGROUP_Tickables);}
UWorld* UAetherCommandClient::GetTickableGameObjectWorld() const{return GetWorld();}
void UAetherCommandClient::Deinitialize()
{
    ResetContainer();Controller.Reset();Channel.Invalidate();Owner.Reset();Profile.Reset();Assembly={};Pending.Reset();OnChanged.Clear();OnResult.Clear();
    Super::Deinitialize();
}

void UAetherCommandClient::ResetContainer()
{Container.Reset();ContainerContext.Invalidate();RequestedContainer.Reset();ContainerAssembly={};ContainerWorldRevision=-1;AssemblyWorldRevision=-1;NextContainerSync=0;}
bool UAetherCommandClient::OpenContainer(const FString& Id)
{
    if(!Matches(Controller.Get(),Channel)||!Profile.IsSet()||Id.IsEmpty()||Id.Len()>96)return false;
    CloseContainer();ContainerContext=FGuid::NewGuid();RequestedContainer=Id;
    GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>()->OpenPage(EAetherMenuPage::Inventory);
    RefreshContainer();OnChanged.Broadcast();return true;
}
void UAetherCommandClient::CloseContainer()
{
    const FGuid Old=ContainerContext;ResetContainer();
    if(Old.IsValid()&&Matches(Controller.Get(),Channel))
    {FAetherV10ContainerQuery Q;Q.Channel=Channel;Q.Context=Old;Controller->ServerV10ContainerQuery(Q);}
    if(Old.IsValid())OnChanged.Broadcast();
}
void UAetherCommandClient::RefreshContainer()
{
    if(!ContainerContext.IsValid()||!Matches(Controller.Get(),Channel)||FPlatformTime::Seconds()<NextContainerSync)return;
    NextContainerSync=FPlatformTime::Seconds()+2;
    FAetherV10ContainerQuery Q;Q.Channel=Channel;Q.Context=ContainerContext;Q.TargetId=RequestedContainer;
    Controller->ServerV10ContainerQuery(Q);
}
void UAetherCommandClient::ReceiveContainerClosed(AAetherPlayerController* C,FGuid Id,FGuid Context)
{if(Matches(C,Id)&&ContainerContext==Context){ResetContainer();OnChanged.Broadcast();}}
void UAetherCommandClient::ReceiveContainerChunk(const FAetherV10SnapshotChunk& P)
{
    if(!ContainerContext.IsValid()||P.Context!=ContainerContext||P.WorldRevision<0||P.WorldRevision==MAX_int64||
        (Container.IsSet()&&P.Revision<Container->Revision))return;
    auto& A=ContainerAssembly;
    if(P.Offset==0)
    {
        if(A.Transfer.IsValid()&&P.Revision<A.Revision)return;
        A={};A.Transfer=P.Transfer;A.Revision=P.Revision;A.Total=P.Total;A.Checksum=P.Checksum;AssemblyWorldRevision=P.WorldRevision;A.Bytes.Reserve(P.Total);
    }
    if(A.Transfer!=P.Transfer||A.Revision!=P.Revision||A.Total!=P.Total||A.Checksum!=P.Checksum||
        AssemblyWorldRevision!=P.WorldRevision||uint32(A.Bytes.Num())!=P.Offset)return;
    A.LastChunkAt=FPlatformTime::Seconds();A.Bytes.Append(P.Bytes);if(uint32(A.Bytes.Num())!=A.Total)return;
    const auto& D=FAetherV10Definitions::Get();FAetherContainerStateV10 Candidate;FString Why;
    const bool Valid=D.bValid&&FCrc::MemCrc32(A.Bytes.GetData(),A.Bytes.Num())==A.Checksum&&
        AetherContainerCodec::Decode(A.Bytes,D.Items,Candidate,Why)&&Candidate.Revision==A.Revision&&
        Candidate.ContainerId.Equals(RequestedContainer,ESearchCase::CaseSensitive)&&Candidate.Allows(Owner)&&Candidate.bActive;
    A={};if(!Valid){CloseContainer();return;}
    ContainerWorldRevision=AssemblyWorldRevision;Container=MoveTemp(Candidate);OnChanged.Broadcast();
}
