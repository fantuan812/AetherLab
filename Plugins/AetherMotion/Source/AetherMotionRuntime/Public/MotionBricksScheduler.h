#pragma once
#include "AetherMotionTypes.h"
#include "HAL/Runnable.h"
#include "HAL/CriticalSection.h"
#include "Templates/Atomic.h"
class FEvent;
class FRunnableThread;

// 进程共享一个模型执行线程；队列里都是值对象，不持有 Actor/UObject。
class AETHERMOTIONRUNTIME_API FMotionBricksScheduler final : public FRunnable
{
public:
    FMotionBricksScheduler();
    virtual ~FMotionBricksScheduler() override;
    uint64 Register();
    void Unregister(uint64 Id);
    void Configure(EAetherMotionBackend Backend,uint32 Threads);
    bool Submit(FAetherMotionInput Input);
    bool Take(uint64 Id,FAetherMotionResult& Result);
    bool IsPending(uint64 Id) const;
    FString Diagnostic() const;
    virtual uint32 Run() override;
    virtual void Stop() override;
private:
    struct FEntry
    {
        FAetherMotionStamp Latest;
        TOptional<FAetherMotionInput> Pending;
        TOptional<FAetherMotionResult> Result;
        bool bExecuting=false;
        double QueuedAt=0;
    };
    mutable FCriticalSection Mutex;
    TMap<uint64,FEntry> Entries;
    uint64 NextId=1,Configuration=1;
    EAetherMotionBackend Backend=EAetherMotionBackend::Traditional;
    uint32 Threads=2;
    FString State=TEXT("传统动画");
    FEvent* Wake=nullptr;
    FRunnableThread* Thread=nullptr;
    TAtomic<bool> bStopping{false};
};
AETHERMOTIONRUNTIME_API FMotionBricksScheduler& AetherMotionScheduler();
