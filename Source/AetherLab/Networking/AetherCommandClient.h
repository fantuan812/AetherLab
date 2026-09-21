#pragma once
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "Profile/AetherProfileState.h"
#include "World/AetherContainerState.h"
#include "Contracts/AetherPlayerCommand.h"
#include "Networking/AetherV10Packets.h"
#include "AetherCommandClient.generated.h"

class AAetherPlayerController;
DECLARE_MULTICAST_DELEGATE(FOnAetherNativeProfileChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAetherNativeCommandResult,const FAetherCommandResult&);

// 请求由 LocalPlayer 拥有，关页、换 Pawn 或重建 Controller 不重建 CommandId。
// 只存内存，不写个人存档；跨进程恢复仍由持久回执及后续客户端恢复方案负责。
UCLASS()
class AETHERLAB_API UAetherCommandClient : public ULocalPlayerSubsystem,public FTickableGameObject
{
    GENERATED_BODY()
public:
    const TOptional<FAetherProfileStateV10>& GetProfile() const{return Profile;}
    bool OpenContainer(const FString& Id);
    void CloseContainer();
    void RefreshContainer();
    const TOptional<FAetherContainerStateV10>& GetContainer() const{return Container;}
    FGuid GetContainerContext() const{return ContainerContext;}
    int64 GetContainerWorldRevision() const{return ContainerWorldRevision;}
    void ReceiveContainerClosed(AAetherPlayerController* C,FGuid ChannelId,FGuid Context);
    FGuid GetChannel() const{return Channel;}
    const FString& GetOwnerIdentity() const{return Owner;}
    bool HasPending() const;
    bool Submit(FGuid ExpectedChannel,const FString& ExpectedOwner,const TArray<uint8>& FrozenBytes,FString& Reason);
    // 重连只恢复快照，不自动执行上一个 Pawn 的意图；用户可明确查询/重试同一拥有者的原请求。
    bool RetryPending();
    void RequestSnapshot();
    void ReceiveChannel(AAetherPlayerController* C,FGuid NewChannel,const FString& CanonicalOwner,FGuid Realm);
    void DetachController(AAetherPlayerController* C);
    void ReceiveReply(AAetherPlayerController* C,const FAetherV10ReplyPacket& Packet);
    void ReceiveChunk(AAetherPlayerController* C,const FAetherV10SnapshotChunk& Chunk);
    FOnAetherNativeProfileChanged OnChanged;
    FOnAetherNativeCommandResult OnResult;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;
    virtual void Deinitialize() override;
private:
    struct FPending
    {
        FString Owner;
        FGuid Id,AuthorizedChannel,Realm;
        TArray<uint8> Bytes;
        TOptional<FAetherCommandResult> Receipt;
        int32 Attempts=0;
        double NextAttempt=0;
    };
    struct FAssembly
    {
        FGuid Transfer;
        int64 Revision=-1;
        uint32 Total=0,Checksum=0;
        double LastChunkAt=0;
        TArray<uint8> Bytes;
    };
    bool Matches(AAetherPlayerController* C,FGuid PacketChannel) const;
    int32 PendingIndex() const;
    void SendPending();
    void RetirePublished();
    TWeakObjectPtr<AAetherPlayerController> Controller;
    FGuid Channel,Realm;
    FString Owner;
    TOptional<FAetherProfileStateV10> Profile;
    FAssembly Assembly,ContainerAssembly;
    TOptional<FAetherContainerStateV10> Container;
    FGuid ContainerContext;
    FString RequestedContainer;
    int64 ContainerWorldRevision=-1,AssemblyWorldRevision=-1;
    double NextContainerSync=0;
    void ReceiveContainerChunk(const FAetherV10SnapshotChunk& Chunk);
    void ResetContainer();
    TArray<FPending> Pending;
    double NextSync=0;
};
