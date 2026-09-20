#pragma once
#include "Contracts/AetherTransactionalStore.h"
#include "World/AetherWorldState.h"
#include "Templates/Function.h"

// Capture 必须在游戏线程调用，只能替换自己拥有的领域；以最新快照为底保留并发交易写入的事实。
using FAetherCaptureWorldCheckpoint=TFunction<bool(const FAetherWorldStateV10&,FAetherWorldStateV10&,FString&)>;
enum class EAetherWorldCheckpointPhase:uint8 {Idle,Reading,Writing,Complete,Failed,Stopped};
struct FAetherWorldCheckpointResult
{
    EAetherStoreCode Code=EAetherStoreCode::Unavailable;
    TOptional<FAetherWorldStateV10> World;
    FString Detail;
};
class AETHERGAMEPLAY_API FAetherWorldCheckpoint
{
public:
    explicit FAetherWorldCheckpoint(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> InStore);
    bool Start(FAetherCaptureWorldCheckpoint Capture,FString& Reason);
    void Poll();
    void Stop();
    EAetherWorldCheckpointPhase Phase() const{return State;}
    const FAetherWorldCheckpointResult& Result() const{return Outcome;}
private:
    void ReadLatest();
    void Fail(EAetherStoreCode Code,FString Reason);
    TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store;
    FAetherCaptureWorldCheckpoint Capture;
    EAetherWorldCheckpointPhase State=EAetherWorldCheckpointPhase::Idle;
    FAetherWorldCheckpointResult Outcome;
    TFuture<FAetherStoreSnapshotResult> Read;
    TFuture<FAetherStoreReadResult> Write;
    TOptional<FAetherWorldStateV10> Candidate;
    TOptional<FAetherAggregateWrite> Encoded;
    int32 Conflicts=0;
    bool bPolling=false;
};
