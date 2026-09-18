#pragma once
#include "Contracts/AetherTransactionalStore.h"

#if WITH_DEV_AUTOMATION_TESTS
enum class EAetherStoreFault : uint8 { None, AfterFirstWrite, BeforeCommit, AfterCommitBeforeReply, CrashBeforeCommit, CrashAfterCommit };
#endif

struct FAetherSqliteOptions
{
    FString DatabasePath;
    int32 QueueCapacity = 64;
#if WITH_DEV_AUTOMATION_TESTS
    // 只存在于开发构建；每次提交原子取走一次，不允许客户端控制故障注入。
    TSharedPtr<TAtomic<EAetherStoreFault>, ESPMode::ThreadSafe> Fault;
#endif
};
struct FAetherSqliteOpenResult
{
    EAetherStoreCode Code = EAetherStoreCode::Unavailable;
    TSharedPtr<IAetherTransactionalStore, ESPMode::ThreadSafe> Store;
    FString Detail;
};

namespace AetherSQLite
{
    // 启动/离线迁移阶段等待连接初始化；游戏中的所有读写均使用有界异步队列。
    AETHERGAMEPLAY_API FAetherSqliteOpenResult Open(FAetherSqliteOptions Options);
    AETHERGAMEPLAY_API FString RuntimeVersion();
}
