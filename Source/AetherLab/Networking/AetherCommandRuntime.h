#pragma once
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Commands/AetherProfileCoordinator.h"
#include "Commands/AetherServerFactCoordinator.h"
#include "Networking/AetherV10Packets.h"
#include "AetherCommandRuntime.generated.h"

class AAetherPlayerController;
struct FAetherCommandRuntimeImpl;
using FAetherResolveConnectedContext=TFunction<bool(AAetherPlayerController&,const FAetherPlayerCommand&,const FAetherProfileStateV10&,FAetherProfileCommandContext&)>;

// 返回 true 之前，启动适配器必须已幂等发布当前 Pawn/ASC 以及涉及的世界、容器事实。
using FAetherPublishConnectedState=TFunction<bool(AAetherPlayerController&,const FAetherProfileStateV10&,const FAetherWorldStateV10*,const FAetherContainerStateV10*)>;

// 只负责连接、流量和游戏线程发布；规则留在原生协调者，后台 SQLite 线程不捕获 UObject。
UCLASS()
class AETHERLAB_API UAetherCommandRuntime : public UGameInstanceSubsystem,public FTickableGameObject
{
    GENERATED_BODY()
public:
    UAetherCommandRuntime();
    virtual ~UAetherCommandRuntime() override;
    // 由完成迁移/恢复的服务器启动流程注入。不会自行打开、创建、导入或覆盖玩家存档。
    bool InstallBackend(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store,FAetherResolveConnectedContext Resolve,FAetherPublishConnectedState Publish,FString& Reason);
    bool IsInstalled() const;
    bool SetBackendDomain(FGuid Realm);
    // 地图切换撤销旧连接与回调，排空已接受的写入后才允许重新安装。
    void UninstallBackend();
    // 含正在退出的后端；新世界必须等旧事务收尾后再打开同一数据库。
    bool HasBackend() const;
    bool DrainBackend(double Seconds=5);
    void SetContainerAuthorizer(TFunction<bool(AAetherPlayerController&,const FString&,bool)> Authorize);
    void QueryContainer(AAetherPlayerController* Controller,const FAetherV10ContainerQuery& Query);
    void SetContainerPublisher(TFunction<void(const FAetherContainerStateV10&)> Publisher);
    void SetWorldPublisher(TFunction<void(const FAetherWorldStateV10&)> Publisher);
    // 可信场景/战斗事件入口；没有对应客户端 RPC。
    bool ObserveServerFact(FAetherServerFact Event,FString& Reason);
    bool HasPendingServerFact(const FString& CharacterId,FName Fact) const;
    bool BindVerifiedPlayer(AAetherPlayerController* Controller,const FString& CanonicalCharacterId);
    void UnbindPlayer(AAetherPlayerController* Controller);
    void NotifyPawnChanged(AAetherPlayerController* Controller);
    // 现场机制输入使用通道内单调序号，不进入持久命令重试队列。
    bool AuthorizeSceneInput(AAetherPlayerController* Controller,const FAetherV10CommandPacket& Packet,uint64 Sequence,FAetherPlayerCommand& Command);
    void Receive(AAetherPlayerController* Controller,const FAetherV10CommandPacket& Packet);
    void RequestSnapshot(AAetherPlayerController* Controller,FGuid Channel);
    void AcknowledgeSnapshot(AAetherPlayerController* Controller,FGuid Channel,FGuid Transfer,uint32 NextOffset);
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;
    virtual void Deinitialize() override;
private:
    TUniquePtr<FAetherCommandRuntimeImpl> Impl;
};
