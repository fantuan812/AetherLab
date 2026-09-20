#pragma once
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Persistence/AetherWorldBootstrap.h"
#include "Persistence/AetherWorldCheckpoint.h"
#include "Networking/AetherCommandRuntime.h"
#include "AetherNativePersistence.generated.h"

enum class EAetherNativePersistencePhase:uint8 {Dormant,Inspecting,Auditing,Prepared,Active,Failed,Stopped};
using FAetherRestoreNativeWorld=TFunction<bool(const FAetherWorldStateV10&,const TMap<FString,int64>&,const TArray<FAetherContainerRestoreDescriptor>&,FString&)>;

// 服务器生产存储生命周期：Prepare -> DTO 审计 -> 场景恢复 -> 安装命令协调者。
// 对外只读状态区分 Prepared 与 Active，不能以“数据库可读”假称整个游戏已恢复。
UCLASS()
class AETHERLAB_API UAetherNativePersistence : public UGameInstanceSubsystem,public FTickableGameObject
{
    GENERATED_BODY()
public:
    // 仅供服务器装配流程；Prefix 不能由玩家 RPC 指定，不接受任意数据库绝对路径。
    bool Prepare(const FString& SavePrefix,bool AllowNewWorld,FString& Reason);
    bool Activate(FAetherResolveConnectedContext Resolve,FAetherPublishConnectedState Publish,FAetherRestoreNativeWorld Restore,FString& Reason);
    // Active 下服务器登录先读已有档案，缺失才创建；不得直接把初始候选赋给 PlayerState。
    TFuture<FAetherStoreReadResult> LoadOrCreateProfile(const FString& ServerCharacterId);
    // 正常周期物理保存；完成前不能据此卸载实体。一次仅允许一个检查点，避免累积过时快照。
    TFuture<FAetherWorldCheckpointResult> SaveLoadedPhysics();
    bool IsSavingWorld() const{return Checkpoint.IsValid();}
    EAetherNativePersistencePhase Phase() const{return State;}
    bool OwnsWriteAuthority() const{return State!=EAetherNativePersistencePhase::Dormant&&State!=EAetherNativePersistencePhase::Stopped;}
    const FString& Failure() const{return Detail;}
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;
    virtual void Deinitialize() override;
private:
    void Fail(FString Reason);
    EAetherNativePersistencePhase State=EAetherNativePersistencePhase::Dormant;
    FString Prefix,Detail;
    bool AllowFresh=false,bActivating=false,bPollingLogins=false;
    TUniquePtr<FAetherWorldCheckpoint> Checkpoint;
    TUniquePtr<TPromise<FAetherWorldCheckpointResult>> CheckpointPromise;
    void PollCheckpoint();
    float CheckpointElapsed=0;
    TSharedPtr<IAetherTransactionalStore,ESPMode::ThreadSafe> Store;
    TUniquePtr<FAetherWorldBootstrap> Bootstrap;
    TFuture<FAetherStoreSnapshotResult> Probe;
    struct FLogin
    {
        FString Identity;
        TFuture<FAetherStoreReadResult> Read;
        TPromise<FAetherStoreReadResult> Result;
        bool bCreating=false;
    };
    TArray<TUniquePtr<FLogin>> Logins;
    void PollLogins();
};
