#pragma once
#include "AetherMotionTypes.h"
#include "HAL/Runnable.h"
#include "HAL/CriticalSection.h"
#include "Templates/Atomic.h"
class FEvent;
class FRunnableThread;

enum class EAetherMotionAttemptOutcome:uint8 {Succeeded,Failed,NoModel,Discarded};
struct FAetherMotionAttemptSample
{
    uint64 Configuration=0,AgentId=0,RequestSequence=0;
    EAetherMotionBackend Requested=EAetherMotionBackend::Traditional,Actual=EAetherMotionBackend::Traditional;
    EAetherMotionAttemptOutcome Outcome=EAetherMotionAttemptOutcome::NoModel;
    FString ModelId,NativeRevision,Style;
    bool bColdStart=false;
    double QueueMilliseconds=0,ModelLoadMilliseconds=0,GenerationMilliseconds=0,InferenceMilliseconds=-1;
    // 调度器没有渲染线程、最终姿态和画面回执；未知阶段不写成 0 毫秒。
    double RetargetMilliseconds=-1,VisibleMilliseconds=-1;
};
struct FAetherMotionSchedulerMetrics
{
    int32 Agents=0,NativeAgents=0,Pending=0,Executing=0;
    uint64 NativeCalls=0,NativeFailures=0;
    uint64 DiscardedResults=0;
    uint64 Configuration=1,WindowSuccesses=0,WindowFailures=0,WindowNoModel=0,WindowDiscarded=0;
    EAetherMotionBackend RequestedBackend=EAetherMotionBackend::Traditional,ActualBackend=EAetherMotionBackend::Traditional;
    int32 QueueSampleCount=0,GenerationSampleCount=0,ColdStartSamples=0;
    double QueueP95Milliseconds=0,GenerationP95Milliseconds=0,InferenceP95Milliseconds=0;
    double QueueMaxMilliseconds=0,GenerationMaxMilliseconds=0;
    FString ContactQualityStatus=TEXT("insufficient_samples");
    TArray<FAetherMotionAttemptSample> RecentAttempts;
};

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
    FAetherMotionSchedulerMetrics Inspect() const;
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
    FAetherMotionSchedulerMetrics Metrics;
    TArray<double> QueueSamples,GenerationSamples,InferenceSamples;
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
