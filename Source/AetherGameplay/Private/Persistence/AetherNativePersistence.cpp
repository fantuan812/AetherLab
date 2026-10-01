#include "Persistence/AetherNativePersistence.h"
#include "Persistence/AetherSaveStartupPolicy.h"
#include "HAL/FileManager.h"
#include "Persistence/AetherNativeWorldPhysics.h"
#include "Persistence/AetherSqliteStore.h"
#include "Profile/AetherProfileCodec.h"
#include "Definitions/AetherV10Definitions.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "Startup/AetherStartupSettings.h"

bool UAetherNativePersistence::Prepare(const FString& InPrefix,bool NewWorld,FString& Reason)
{
    check(IsInGameThread());
    if(bStoppingScene){Reason=TEXT("Previous scene is still stopping");return false;}
    if(BoundScene.IsValid()&&BoundScene.Get()!=GetWorld()){StopScene();State=EAetherNativePersistencePhase::Dormant;}
    if(State!=EAetherNativePersistencePhase::Dormant||!GetWorld()||GetWorld()->GetNetMode()==NM_Client||!AetherSaveStartup::ValidPrefix(InPrefix))
    {Reason=TEXT("Invalid native startup state, server world or save prefix");return false;}
    double Deadline=0;
    if(!GetDefault<UAetherStartupSettings>()->DrainDeadline(FPlatformTime::Seconds(),Deadline,Reason))return false;
    auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>();
    if(Runtime&&Runtime->IsInstalled())
    {Reason=TEXT("An active native backend still owns the current scene");return false;}
    // 自此锁住旧总写入口；打开失败也保持失败状态，绝不能回退旧档继续写出分叉进度。
    BoundScene=GetWorld();SceneGeneration=FGuid::NewGuid();Prefix=InPrefix;AllowFresh=NewWorld;
    Startup={};Startup.AttemptId=SceneGeneration;BackendDrainDeadline=Deadline;
    if(Runtime&&Runtime->HasBackend())
    {
        // 只接受一次等待请求。旧 Runtime 仍持有已接受事务与 Store，新场景尚未打开数据库。
        State=EAetherNativePersistencePhase::WaitingForBackend;PublishStartup(EAetherStartupStage::WaitingForBackend);
        Reason.Reset();return true;
    }
    return OpenPreparedStore(Reason);
}
bool UAetherNativePersistence::OpenPreparedStore(FString& Reason)
{
    check(IsInGameThread());
    if(!BoundScene.IsValid()||BoundScene.Get()!=GetWorld()||Store||
        (GetGameInstance()->GetSubsystem<UAetherCommandRuntime>()&&GetGameInstance()->GetSubsystem<UAetherCommandRuntime>()->HasBackend()))
    {Reason=TEXT("Native store cannot open before the previous backend has released ownership");return false;}
    State=EAetherNativePersistencePhase::Inspecting;PublishStartup(EAetherStartupStage::ReadingStorage);
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("V10State")/Prefix/TEXT("state.sqlite"));
    auto Open=AetherSQLite::Open(MoveTemp(O));
    if(!Open.Store){Fail(Open.Detail,EAetherStartupFailure::StorageOpenFailed);Reason=Detail;return false;}
    Store=MoveTemp(Open.Store);
    FAetherStoreSnapshotQuery Q;Q.Keys={{EAetherAggregateKind::World,TEXT("Main")}};Q.bIncludeProfileRevisions=true;Q.bIncludeContainerCount=true;
    Probe=Store->ReadSnapshot(MoveTemp(Q));Reason.Reset();return true;
}
void UAetherNativePersistence::Tick(float DeltaSeconds)
{
    if(State==EAetherNativePersistencePhase::WaitingForBackend)
    {
        if(!BoundScene.IsValid()||BoundScene.Get()!=GetWorld()){StopScene();State=EAetherNativePersistencePhase::Dormant;return;}
        if(FPlatformTime::Seconds()>=BackendDrainDeadline)
        {Fail(TEXT("Previous native backend did not drain before the configured startup deadline"),EAetherStartupFailure::BackendDrainTimedOut);return;}
        if(auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>();Runtime&&Runtime->HasBackend())return;
        FString Reason;if(!OpenPreparedStore(Reason)){if(State!=EAetherNativePersistencePhase::Failed)Fail(Reason,EAetherStartupFailure::StorageOpenFailed);return;}
    }
    PollCheckpoint();
    PollLogins();
    if(State==EAetherNativePersistencePhase::Active)
    {
        CheckpointElapsed+=FMath::Max(0.f,DeltaSeconds);
        if(CheckpointElapsed>=10&&!Checkpoint){CheckpointElapsed=0;SaveLoadedPhysics();}
    }
    if(State==EAetherNativePersistencePhase::Inspecting)
    {
        if(!Probe.IsValid()||!Probe.IsReady())return;
        const auto R=Probe.Get();Probe={};if(R.Code!=EAetherStoreCode::Found){Fail(R.Detail,EAetherStartupFailure::StorageAuditFailed);return;}
        const bool Existing=R.Values.Contains({EAetherAggregateKind::World,TEXT("Main")});
        bool Historical=false;
        if(!Existing)for(int32 Slot=0;Slot<2;++Slot)
        {
            const FString Base=FPaths::ProjectSavedDir()/TEXT("SaveGames")/(Prefix+FString::FromInt(Slot));
            Historical|=IFileManager::Get().FileExists(*(Base+TEXT(".sav")))||IFileManager::Get().FileExists(*(Base+TEXT(".crc")));
        }
        FString PolicyReason;
        if(!AetherSaveStartup::CanOpen(Existing,!R.ProfileRevisions.IsEmpty()||R.ContainerCount!=0,Historical,AllowFresh,PolicyReason))
        {Fail(PolicyReason,EAetherStartupFailure::StorageAuditFailed);return;}
        // 已有原生世界优先，不因遗留旧档现在损坏而覆盖或拒绝合法的新数据库。
        Bootstrap=MakeUnique<FAetherWorldBootstrap>(Store.ToSharedRef());FString Reason;
        if(!Bootstrap->Start(!Existing&&AllowFresh,Reason)){Fail(Reason,EAetherStartupFailure::StorageAuditFailed);return;}
        State=EAetherNativePersistencePhase::Auditing;PublishStartup(EAetherStartupStage::Auditing);
    }
    if(State==EAetherNativePersistencePhase::Auditing)
    {
        Bootstrap->Poll();
        if(Bootstrap->Phase()==EAetherBootstrapPhase::Failed){Fail(Bootstrap->Failure(),EAetherStartupFailure::StorageAuditFailed);return;}
        if(Bootstrap->Phase()==EAetherBootstrapPhase::Ready){State=EAetherNativePersistencePhase::Prepared;PublishStartup(EAetherStartupStage::Restoring);}
    }
}
bool UAetherNativePersistence::Activate(FAetherResolveConnectedContext Resolve,FAetherPublishConnectedState Publish,FAetherRestoreNativeWorld Restore,FString& Reason)
{
    check(IsInGameThread());
    if(State!=EAetherNativePersistencePhase::Prepared||bActivating||!Bootstrap||!Bootstrap->World().IsSet()||!Store||
        !BoundScene.IsValid()||BoundScene.Get()!=GetWorld()||!SceneGeneration.IsValid()||!Resolve||!Publish||!Restore)
    {Reason=TEXT("Native backend requires audited data and concrete restore/context/publication adapters");return false;}
    TGuardValue<bool> Guard(bActivating,true);
    // Restore 会生成 Actor 并调用场景适配器；同步退出会 Stop/释放 Bootstrap。
    // 实参必须属于这次调用，不能让回调继续读取已释放或被新审计替换的容器。
    const auto World=Bootstrap->World().GetValue();const auto Profiles=Bootstrap->ProfileRevisions();
    const auto Containers=Bootstrap->Containers();const auto Backend=Store;
    const auto Scene=BoundScene;const FGuid Generation=SceneGeneration;
    const bool Restored=Restore(World,Profiles,Containers,Reason);
    // 先检查代次再处理失败，旧回调既不能安装新后端，也不能 Fail 新场景。
    if(State!=EAetherNativePersistencePhase::Prepared||!Scene.IsValid()||BoundScene!=Scene||
        SceneGeneration!=Generation||Store!=Backend||GetWorld()!=Scene.Get())
    {Reason=TEXT("Scene changed during native restore");return false;}
    if(!Restored)
    {Fail(Reason);return false;}
    auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>();
    if(!Runtime||!Runtime->InstallBackend(Backend.ToSharedRef(),MoveTemp(Resolve),MoveTemp(Publish),Reason))
    {Fail(Reason);return false;}
    State=EAetherNativePersistencePhase::Active;Detail.Reset();return true;
}
TFuture<FAetherStoreReadResult> UAetherNativePersistence::CreateEmptyContainer(FAetherContainerStateV10 C)
{
    check(IsInGameThread());if(State==EAetherNativePersistencePhase::Active&&Store)return Store->CreateEmptyContainer(MoveTemp(C),FAetherV10Definitions::Get().Items);
    TPromise<FAetherStoreReadResult> P;auto F=P.GetFuture();P.SetValue(FAetherStoreReadResult());return F;
}
TFuture<FAetherStoreReadResult> UAetherNativePersistence::LoadOrCreateProfile(const FString& Identity)
{
    check(IsInGameThread());
    auto Job=MakeUnique<FLogin>();Job->Identity=Identity;auto Future=Job->Result.GetFuture();
    bool Valid=!Identity.IsEmpty()&&Identity.Len()<=32;for(TCHAR C:Identity)Valid&=C>=32;
    if(State!=EAetherNativePersistencePhase::Active||bActivating||!Store||!Valid||Logins.Num()>=16||
        Logins.ContainsByPredicate([&](const auto& L){return L->Identity.Equals(Identity,ESearchCase::IgnoreCase);}))
    {
        FAetherStoreReadResult R;R.Code=Valid?EAetherStoreCode::Busy:EAetherStoreCode::Invalid;
        R.Detail=TEXT("Profile service unavailable, already loading or identity invalid");Job->Result.SetValue(MoveTemp(R));return Future;
    }
    Job->Read=Store->Read({EAetherAggregateKind::Profile,Identity});Logins.Add(MoveTemp(Job));return Future;
}
void UAetherNativePersistence::PollLogins()
{
    if(bPollingLogins)return;TGuardValue<bool> Guard(bPollingLogins,true);
    // Promise 可同步触发退出或再次登录，先完成整批队列变更，再在独立数组中通知消费者。
    struct FCompleted {TUniquePtr<FLogin> Job;FAetherStoreReadResult Result;};
    TArray<FCompleted> Completed;
    for(int32 Index=Logins.Num()-1;Index>=0;--Index)
    {
        auto& Job=*Logins[Index];if(!Job.Read.IsValid()||!Job.Read.IsReady())continue;
        auto R=Job.Read.Get();Job.Read={};
        if(State!=EAetherNativePersistencePhase::Active){R={};R.Code=EAetherStoreCode::Unavailable;}
        else if(R.Code==EAetherStoreCode::Missing&&!Job.bCreating)
        {
            FAetherStoredAggregate Initial;FString Reason;
            if(AetherProfileBootstrap::BuildNew(Job.Identity,Initial,Reason))
            {Job.bCreating=true;Job.Read=Store->CreateProfile(MoveTemp(Initial));continue;}
            R.Code=EAetherStoreCode::Invalid;R.Detail=Reason;
        }
        if(R.Code==EAetherStoreCode::Found)
        {
            const auto& D=FAetherV10Definitions::Get();FAetherProfileStateV10 P;FString Reason;
            if(!R.Value.IsSet()||R.Value->SchemaVersion!=10||!AetherProfileCodec::Decode(R.Value->Payload,D.Items,D.Skills,D.Rules,P,Reason)||
                P.Revision!=R.Value->Revision||!P.CharacterId.Equals(Job.Identity,ESearchCase::CaseSensitive))
            {R.Code=EAetherStoreCode::Corrupt;R.Value.Reset();R.Detail=Reason.IsEmpty()?TEXT("Profile identity/revision mismatch"):Reason;}
        }
        auto Done=MoveTemp(Logins[Index]);Logins.RemoveAtSwap(Index);
        Completed.Add({MoveTemp(Done),MoveTemp(R)});
    }
    for(auto& Done:Completed)Done.Job->Result.SetValue(MoveTemp(Done.Result));
}
TFuture<FAetherWorldCheckpointResult> UAetherNativePersistence::SaveLoadedPhysics(){return SaveWorldMutation({});}
TFuture<FAetherWorldCheckpointResult> UAetherNativePersistence::SaveWorldMutation(FAetherCaptureWorldCheckpoint Mutation)
{
    check(IsInGameThread());auto Promise=MakeUnique<TPromise<FAetherWorldCheckpointResult>>();auto Future=Promise->GetFuture();
    if(State!=EAetherNativePersistencePhase::Active||Checkpoint||!Store||!BoundScene.IsValid()||!SceneGeneration.IsValid()||BoundScene.Get()!=GetWorld())
    {FAetherWorldCheckpointResult R;R.Code=EAetherStoreCode::Busy;Promise->SetValue(MoveTemp(R));return Future;}
    Checkpoint=MakeShared<FAetherWorldCheckpoint>(Store.ToSharedRef());FString Why;
    const TWeakObjectPtr<UWorld> Scene=BoundScene;
    const TWeakObjectPtr<UAetherNativePersistence> Self=this;const FGuid Generation=SceneGeneration;
    const auto Capture=DomainCapture;
    if(!Checkpoint->Start([Self,Scene,Generation,Capture,Mutation=MoveTemp(Mutation)](const auto& Previous,auto& Candidate,FString& Reason){
        const auto Current=[&]{return Self.IsValid()&&Scene.IsValid()&&Self->BoundScene==Scene&&
            Self->SceneGeneration==Generation&&Self->State==EAetherNativePersistencePhase::Active;};
        if(!Current()){Reason=TEXT("Scene changed during checkpoint");return false;}
        if(!(Capture?Capture(Previous,Candidate,Reason):AetherNativeWorldPhysics::CaptureLoaded(*Scene.Get(),Previous,Candidate,Reason)))return false;
        // 领域捕获可触发 Travel/退出，旧请求不能在新场景继续执行变更。
        if(!Current()){Reason=TEXT("Scene changed during checkpoint capture");return false;}
        return !Mutation||Mutation(Previous,Candidate,Reason);
    },Why))
    {Checkpoint.Reset();FAetherWorldCheckpointResult R;R.Code=EAetherStoreCode::Invalid;R.Detail=Why;Promise->SetValue(MoveTemp(R));return Future;}
    CheckpointPromise=MoveTemp(Promise);return Future;
}
void UAetherNativePersistence::PollCheckpoint()
{
    const auto Active=Checkpoint;const FGuid Generation=SceneGeneration;
    if(!Active)return;Active->Poll();
    // StopScene 可以撤下甚至替换成员；局部拥有者保护栈帧，代次保护新场景。
    if(Checkpoint!=Active||SceneGeneration!=Generation)return;
    const auto Phase=Active->Phase();
    if(Phase!=EAetherWorldCheckpointPhase::Complete&&Phase!=EAetherWorldCheckpointPhase::Failed)return;
    auto Result=Active->Result();auto Promise=MoveTemp(CheckpointPromise);const auto Published=CheckpointPublished;Checkpoint.Reset();
    // 先撤下运行中标志，再发布持久确认，回调可以安全安排下一次检查点。
    if(Result.Code!=EAetherStoreCode::Committed&&Result.Code!=EAetherStoreCode::Replayed)
        UE_LOG(LogTemp,Warning,TEXT("AETHER_NATIVE_CHECKPOINT_DEFERRED code=%d reason=%s"),int32(Result.Code),*Result.Detail);
    // 发布者也可能同步 ReleaseScene/ConfigureCheckpoints，不能销毁正在调用的成员函数对象。
    if(Result.World.IsSet()&&(Result.Code==EAetherStoreCode::Committed||Result.Code==EAetherStoreCode::Replayed)&&Published)Published(Result.World.GetValue());
    if(Promise)Promise->SetValue(MoveTemp(Result));
}
void UAetherNativePersistence::PublishStartup(EAetherStartupStage Stage,EAetherStartupFailure Code)
{
    if(Startup.Stage==Stage&&Startup.FailureCode==Code)return;
    Startup.Stage=Stage;Startup.FailureCode=Code;
    if(Startup.Sequence<MAX_uint32)++Startup.Sequence;
}
void UAetherNativePersistence::Fail(FString Reason,EAetherStartupFailure Code)
{
    Detail=Reason.IsEmpty()?TEXT("Native persistence preparation failed"):MoveTemp(Reason);
    State=EAetherNativePersistencePhase::Failed;
    PublishStartup(EAetherStartupStage::Failed,Code);
    // 失败只冻结入口并保留磁盘，不清库、不覆盖来源、不转回 v9。
}
bool UAetherNativePersistence::IsTickable() const
{return !IsTemplate()&&(State==EAetherNativePersistencePhase::WaitingForBackend||State==EAetherNativePersistencePhase::Inspecting||State==EAetherNativePersistencePhase::Auditing||State==EAetherNativePersistencePhase::Active||!Logins.IsEmpty()||Checkpoint.IsValid());}
TStatId UAetherNativePersistence::GetStatId() const{RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherNativePersistence,STATGROUP_Tickables);}
UWorld* UAetherNativePersistence::GetTickableGameObjectWorld() const{return GetWorld();}
void UAetherNativePersistence::StopScene()
{
    if(bStoppingScene)return;TGuardValue<bool> Guard(bStoppingScene,true);
    State=EAetherNativePersistencePhase::Stopped;SceneGeneration.Invalidate();DomainCapture={};CheckpointPublished={};
    auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>();
    const bool RuntimeOwnsStore=Runtime&&Runtime->HasBackend();
    if(RuntimeOwnsStore)Runtime->UninstallBackend();
    auto Pending=MoveTemp(Logins);for(auto& Job:Pending){FAetherStoreReadResult R;R.Code=EAetherStoreCode::Unavailable;Job->Result.SetValue(MoveTemp(R));}
    if(Checkpoint)Checkpoint->Stop();Checkpoint.Reset();
    auto PendingCheckpoint=MoveTemp(CheckpointPromise);
    Probe={};if(Bootstrap)Bootstrap->Stop();Bootstrap.Reset();
    // 先停生产者，再收尾命令。重入/超时情况下 Runtime 继续持有并最终关闭 Store；
    // 这里绝不能 Close，否则后续 CAS 重读/提交会得到 Unavailable，丢失已接受事实。
    if(RuntimeOwnsStore)Runtime->DrainBackend();
    else if(Store)Store->Close();
    Store.Reset();
    if(PendingCheckpoint){FAetherWorldCheckpointResult R;R.Code=EAetherStoreCode::Unavailable;R.Detail=TEXT("Shutdown drained writes; reload persisted world before retry");PendingCheckpoint->SetValue(MoveTemp(R));}
    State=EAetherNativePersistencePhase::Stopped;BoundScene.Reset();CheckpointElapsed=0;Prefix.Reset();Detail.Reset();
}

void UAetherNativePersistence::ReleaseScene(UWorld* Scene)
{
    check(IsInGameThread());if(bStoppingScene||BoundScene.Get()!=Scene)return;StopScene();State=EAetherNativePersistencePhase::Dormant;
}
bool UAetherNativePersistence::CancelPreparation(UWorld* Scene,FGuid Attempt)
{
    check(IsInGameThread());
    if(bStoppingScene||!Scene||BoundScene.Get()!=Scene||!Attempt.IsValid()||Attempt!=SceneGeneration||
        State==EAetherNativePersistencePhase::Active||State==EAetherNativePersistencePhase::Dormant||State==EAetherNativePersistencePhase::Stopped)return false;
    StopScene();State=EAetherNativePersistencePhase::Cancelled;PublishStartup(EAetherStartupStage::Cancelled);return true;
}
void UAetherNativePersistence::MarkWorldReady(UWorld* Scene)
{if(BoundScene.Get()==Scene&&State==EAetherNativePersistencePhase::Active)PublishStartup(EAetherStartupStage::WorldReady);}
void UAetherNativePersistence::ReportSceneFailure(UWorld* Scene,const FString& Reason)
{if(BoundScene.Get()==Scene&&State!=EAetherNativePersistencePhase::Failed)Fail(Reason);}
void UAetherNativePersistence::Deinitialize(){StopScene();Super::Deinitialize();}
