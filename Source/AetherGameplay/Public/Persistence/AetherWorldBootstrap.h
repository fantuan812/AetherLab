#pragma once
#include "Contracts/AetherTransactionalStore.h"
#include "Definitions/AetherV10Definitions.h"
#include "World/AetherWorldState.h"
#include "World/AetherContainerState.h"

enum class EAetherBootstrapPhase:uint8 {Idle,Reading,Initializing,Profiles,Containers,Rechecking,Ready,Failed,Stopped};
struct FAetherContainerRestoreDescriptor
{
    FString Id,Owner,Region;
    EAetherContainerKind Kind=EAetherContainerKind::SharedChest;
    FVector Location=FVector::ZeroVector;
    bool bActive=false;
    int64 Revision=-1;
};

// 后台仅传值；Poll 在游戏线程取已就绪 Future，不等待磁盘、不持有 Actor。
// Ready 表示全库 DTO/引用审计完成，实际地图/ASC 恢复成功后才允许安装命令后端。
class AETHERGAMEPLAY_API FAetherWorldBootstrap
{
public:
    explicit FAetherWorldBootstrap(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store);
    ~FAetherWorldBootstrap();
    bool Start(TOptional<FAetherLegacyImport> Legacy,bool AllowFreshWorld,FString& Reason);
    void Poll();
    void Stop();
    EAetherBootstrapPhase Phase() const;
    const FString& Failure() const;
    const TOptional<FAetherWorldStateV10>& World() const;
    const TMap<FString,int64>& ProfileRevisions() const;
    const TArray<FAetherContainerRestoreDescriptor>& Containers() const;
private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};

namespace AetherProfileBootstrap
{
    // 初始物品只在 INSERT 成功时发放。调用者使用 Store.CreateProfile，重连返回原档，不重复 AddNew。
    AETHERGAMEPLAY_API bool BuildNew(const FString& ServerCharacterId,FAetherStoredAggregate& Out,FString& Reason);
}
