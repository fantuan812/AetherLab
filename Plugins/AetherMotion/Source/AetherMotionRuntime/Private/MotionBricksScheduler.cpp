#include "MotionBricksScheduler.h"
#include "MotionBricksModelOwner.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

FMotionBricksScheduler::FMotionBricksScheduler()
{
    Wake=FPlatformProcess::GetSynchEventFromPool(false);
#if !UE_SERVER
    Thread=FRunnableThread::Create(this,TEXT("AetherMotion"),0,TPri_BelowNormal);
#endif
}
FMotionBricksScheduler::~FMotionBricksScheduler()
{
    Stop();if(Thread){Thread->WaitForCompletion();delete Thread;Thread=nullptr;}
    FPlatformProcess::ReturnSynchEventToPool(Wake);
}
void FMotionBricksScheduler::Stop(){bStopping.Store(true);if(Wake)Wake->Trigger();}
uint64 FMotionBricksScheduler::Register()
{
    FScopeLock Lock(&Mutex);if(bStopping.Load()||!Thread||Entries.Num()>=16||NextId==MAX_uint64)return 0;
    const uint64 Id=NextId++;Entries.Add(Id);return Id;
}
void FMotionBricksScheduler::Unregister(uint64 Id)
{FScopeLock Lock(&Mutex);Entries.Remove(Id);Wake->Trigger();}
void FMotionBricksScheduler::Configure(EAetherMotionBackend In,uint32 Count)
{
    FScopeLock Lock(&Mutex);Count=FMath::Clamp(Count,1u,8u);
    if(In==Backend&&Count==Threads)return;
    Backend=In;Threads=Count;++Configuration;
    for(auto& Pair:Entries){Pair.Value.Pending.Reset();Pair.Value.Result.Reset();}
    State=In==EAetherMotionBackend::Traditional?TEXT("传统动画"):TEXT("正在后台准备生成动作");Wake->Trigger();
}
bool FMotionBricksScheduler::Submit(FAetherMotionInput Input)
{
    if(!Input.Stamp.WorldEpoch.IsValid()||!Input.Stamp.PawnEpoch.IsValid()||Input.Stamp.RequestSequence==0||
        !FMath::IsFinite(Input.SimulationTime)||!FMath::IsFinite(Input.SpeedMeters)||Input.SpeedMeters<0||Input.SpeedMeters>20||
        Input.Movement.ContainsNaN()||Input.Facing.ContainsNaN()||!FMath::IsFinite(Input.Priority)||Input.Style.Len()>64||
        (!Input.Context.Roots.IsEmpty()&&!Input.Context.IsValid())||
        (Input.TransitionTarget.IsSet()&&!Input.TransitionTarget->IsValid()))return false;
    FScopeLock Lock(&Mutex);auto* Entry=Entries.Find(Input.AgentId);
    if(!Entry||bStopping.Load()||Backend==EAetherMotionBackend::Traditional||Input.Stamp.RequestSequence<=Entry->Latest.RequestSequence)return false;
    Input.SubmittedAt=FPlatformTime::Seconds();Entry->Latest=Input.Stamp;
    if(!Entry->Pending.IsSet())Entry->QueuedAt=FPlatformTime::Seconds();
    // 在途最多一个、待处理最多一个；只覆盖未开始的输入，没有无界任务队列。
    Entry->Pending=MoveTemp(Input);Wake->Trigger();return true;
}
bool FMotionBricksScheduler::Take(uint64 Id,FAetherMotionResult& Result)
{
    FScopeLock Lock(&Mutex);auto* Entry=Entries.Find(Id);if(!Entry||!Entry->Result.IsSet())return false;
    Result=MoveTemp(Entry->Result.GetValue());Entry->Result.Reset();return true;
}
bool FMotionBricksScheduler::IsPending(uint64 Id) const
{FScopeLock Lock(&Mutex);const auto* E=Entries.Find(Id);return E&&(E->bExecuting||E->Pending.IsSet());}
FString FMotionBricksScheduler::Diagnostic() const
{FScopeLock Lock(&Mutex);return State;}
FAetherMotionSchedulerMetrics FMotionBricksScheduler::Inspect() const
{
    FScopeLock Lock(&Mutex);auto Snapshot=Metrics;Snapshot.Agents=Entries.Num();
    for(const auto& Pair:Entries){Snapshot.Pending+=Pair.Value.Pending.IsSet()?1:0;Snapshot.Executing+=Pair.Value.bExecuting?1:0;}
    return Snapshot;
}
uint32 FMotionBricksScheduler::Run()
{
    FMotionBricksApi Api;TUniquePtr<FMotionBricksModelOwner> Model;TSet<uint64> NativeIds;uint64 LoadedConfiguration=0;
    FString Failure;
    while(!bStopping.Load())
    {
        FAetherMotionInput Work;uint64 Version=0;EAetherMotionBackend Device;uint32 Count;TSet<uint64> Live;bool HasWork=false;
        {
            FScopeLock Lock(&Mutex);Version=Configuration;Device=Backend;Count=Threads;
            for(const auto& Pair:Entries)Live.Add(Pair.Key);
            double Best=-DBL_MAX;uint64 Pick=0;const double Now=FPlatformTime::Seconds();
            for(const auto& Pair:Entries)if(Pair.Value.Pending.IsSet())
            {
                const double Score=Pair.Value.Pending->Priority+FMath::Min(20.,Now-Pair.Value.QueuedAt);
                if(Score>Best){Best=Score;Pick=Pair.Key;}
            }
            if(auto* E=Entries.Find(Pick)){Work=MoveTemp(E->Pending.GetValue());E->Pending.Reset();E->bExecuting=true;HasWork=true;}
        }
        if(Model)for(auto It=NativeIds.CreateIterator();It;++It)if(!Live.Contains(*It)){Model->RemoveAgent(*It);It.RemoveCurrent();}
        if(LoadedConfiguration!=Version)
        {
            Model.Reset();NativeIds.Reset();Failure.Reset();LoadedConfiguration=Version;
            if(Device!=EAetherMotionBackend::Traditional)
            {
                Model=MakeUnique<FMotionBricksModelOwner>(Api);
                EAetherMotionBackend Actual=Device;
                if(Device==EAetherMotionBackend::Automatic)
                {
                    if(Api.Load(Failure))Actual=Api.StagedBackend==TEXT("Vulkan")?EAetherMotionBackend::Vulkan:EAetherMotionBackend::CPU;
                    else Actual=EAetherMotionBackend::Traditional;
                }
                if(Actual==EAetherMotionBackend::Traditional||!Model->Initialize(Actual,Count,Failure))
                {
                    Model.Reset();
                    if(Device==EAetherMotionBackend::Automatic&&Actual==EAetherMotionBackend::Vulkan)
                    {
                        const FString VulkanFailure=Failure;
                        Model=MakeUnique<FMotionBricksModelOwner>(Api);Actual=EAetherMotionBackend::CPU;
                        if(!Model->Initialize(Actual,Count,Failure))Model.Reset();
                        else Failure=TEXT("Vulkan 不可用，回退 CPU：")+VulkanFailure;
                    }
                }
                if(Model)
                {
                    FScopeLock Lock(&Mutex);
                    if(Configuration==Version)State=FString::Printf(TEXT("请求 %s · 实际 %s%s"),
                        Device==EAetherMotionBackend::Automatic?TEXT("自动"):Device==EAetherMotionBackend::CPU?TEXT("CPU"):TEXT("Vulkan"),
                        Actual==EAetherMotionBackend::CPU?TEXT("CPU"):TEXT("Vulkan"),Failure.IsEmpty()?TEXT(""):*FString(TEXT(" · ")+Failure));
                }
            }
            FScopeLock Lock(&Mutex);if(Configuration==Version&&!Model)State=Failure.IsEmpty()?TEXT("传统动画"):TEXT("传统动画 · ")+Failure;
        }
        {FScopeLock Lock(&Mutex);Metrics.NativeAgents=NativeIds.Num();}
        if(!HasWork){Wake->Wait(1000);continue;}
        FAetherMotionResult Result;Result.AgentId=Work.AgentId;Result.Stamp=Work.Stamp;Result.SubmittedAt=Work.SubmittedAt;
        if(Model){Result.Clip=Model->Generate(Work,Result.Reason);NativeIds.Add(Work.AgentId);}
        else Result.Reason=Failure.IsEmpty()?TEXT("传统动画"):Failure;
        {
            FScopeLock Lock(&Mutex);
            Metrics.NativeAgents=NativeIds.Num();if(Model){++Metrics.NativeCalls;if(!Result.Clip)++Metrics.NativeFailures;}
            if(auto* E=Entries.Find(Work.AgentId))
            {
                E->bExecuting=false;
                if(Configuration==Version&&E->Latest.SameIntent(Work.Stamp)&&E->Latest.RequestSequence==Work.Stamp.RequestSequence)
                    E->Result=MoveTemp(Result);
                // 废弃输出后，下次请求携带最后实际消费的四帧；ModelOwner 不沿用未播放的计划。
            }
        }
    }
    // Model 析构先于 Api：线程退出前完成所有句柄释放，再卸载 DLL。
    Model.Reset();return 0;
}
