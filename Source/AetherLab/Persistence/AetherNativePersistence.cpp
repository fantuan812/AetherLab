#include "Persistence/AetherNativePersistence.h"
#include "Persistence/AetherNativeMigrationSource.h"
#include "Persistence/AetherNativeWorldPhysics.h"
#include "Persistence/AetherSqliteStore.h"
#include "Profile/AetherProfileCodec.h"
#include "Definitions/AetherV10Definitions.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"

bool UAetherNativePersistence::Prepare(const FString& InPrefix,bool NewWorld,FString& Reason)
{
    check(IsInGameThread());
    if(BoundScene.IsValid()&&BoundScene.Get()!=GetWorld()){StopScene();State=EAetherNativePersistencePhase::Dormant;}
    if(State!=EAetherNativePersistencePhase::Dormant||!GetWorld()||GetWorld()->GetNetMode()==NM_Client||!AetherNativeMigration::ValidPrefix(InPrefix))
    {Reason=TEXT("Invalid native startup state, server world or save prefix");return false;}
    // 自此锁住旧总写入口；打开失败也保持失败状态，绝不能回退旧档继续写出分叉进度。
    BoundScene=GetWorld();Prefix=InPrefix;AllowFresh=NewWorld;State=EAetherNativePersistencePhase::Inspecting;
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("V10State")/Prefix/TEXT("state.sqlite"));
    auto Open=AetherSQLite::Open(MoveTemp(O));
    if(!Open.Store){Fail(Open.Detail);Reason=Detail;return false;}
    Store=MoveTemp(Open.Store);
    FAetherStoreSnapshotQuery Q;Q.Keys={{EAetherAggregateKind::World,TEXT("Main")}};Q.bIncludeProfileRevisions=true;Q.bIncludeContainerCount=true;
    Probe=Store->ReadSnapshot(MoveTemp(Q));Reason.Reset();return true;
}
void UAetherNativePersistence::Tick(float DeltaSeconds)
{
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
        const auto R=Probe.Get();Probe={};if(R.Code!=EAetherStoreCode::Found){Fail(R.Detail);return;}
        TOptional<FAetherLegacyImport> Import;
        const bool Existing=R.Values.Contains({EAetherAggregateKind::World,TEXT("Main")});
        if(!Existing)
        {
            if(!R.ProfileRevisions.IsEmpty()||R.ContainerCount!=0){Fail(TEXT("Existing data has no world; refusing automatic migration/new world"));return;}
            auto Source=AetherNativeMigration::Prepare(Prefix);
            if(Source.Code==EAetherNativeSourceCode::Invalid){Fail(Source.Detail);return;}
            Import=MoveTemp(Source.Import);
            if(!Import.IsSet()&&!AllowFresh){Fail(TEXT("No verified legacy source; fresh world not authorized"));return;}
        }
        // 已有原生世界优先，不因遗留旧档现在损坏而覆盖或拒绝合法的新数据库。
        Bootstrap=MakeUnique<FAetherWorldBootstrap>(Store.ToSharedRef());FString Reason;
        if(!Bootstrap->Start(MoveTemp(Import),!Existing&&AllowFresh,Reason)){Fail(Reason);return;}
        State=EAetherNativePersistencePhase::Auditing;
    }
    if(State==EAetherNativePersistencePhase::Auditing)
    {
        Bootstrap->Poll();
        if(Bootstrap->Phase()==EAetherBootstrapPhase::Failed){Fail(Bootstrap->Failure());return;}
        if(Bootstrap->Phase()==EAetherBootstrapPhase::Ready)State=EAetherNativePersistencePhase::Prepared;
    }
}
bool UAetherNativePersistence::Activate(FAetherResolveConnectedContext Resolve,FAetherPublishConnectedState Publish,FAetherRestoreNativeWorld Restore,FString& Reason)
{
    check(IsInGameThread());
    if(State!=EAetherNativePersistencePhase::Prepared||bActivating||!Bootstrap||!Bootstrap->World().IsSet()||!Resolve||!Publish||!Restore)
    {Reason=TEXT("Native backend requires audited data and concrete restore/context/publication adapters");return false;}
    TGuardValue<bool> Guard(bActivating,true);
    if(!Restore(Bootstrap->World().GetValue(),Bootstrap->ProfileRevisions(),Bootstrap->Containers(),Reason))
    {Fail(Reason);return false;}
    auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>();
    if(!Runtime||!Runtime->InstallBackend(Store.ToSharedRef(),MoveTemp(Resolve),MoveTemp(Publish),Reason))
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
    if(State!=EAetherNativePersistencePhase::Active||Checkpoint||!Store||!GetWorld())
    {FAetherWorldCheckpointResult R;R.Code=EAetherStoreCode::Busy;Promise->SetValue(MoveTemp(R));return Future;}
    Checkpoint=MakeUnique<FAetherWorldCheckpoint>(Store.ToSharedRef());FString Why;
    const TWeakObjectPtr<UWorld> Scene=GetWorld();
    const auto Capture=DomainCapture;
    if(!Checkpoint->Start([Scene,Capture,Mutation=MoveTemp(Mutation)](const auto& Previous,auto& Candidate,FString& Reason){
        if(!Scene.IsValid()){Reason=TEXT("World destroyed during checkpoint");return false;}
        if(!(Capture?Capture(Previous,Candidate,Reason):AetherNativeWorldPhysics::CaptureLoaded(*Scene.Get(),Previous,Candidate,Reason)))return false;
        return !Mutation||Mutation(Previous,Candidate,Reason);
    },Why))
    {Checkpoint.Reset();FAetherWorldCheckpointResult R;R.Code=EAetherStoreCode::Invalid;R.Detail=Why;Promise->SetValue(MoveTemp(R));return Future;}
    CheckpointPromise=MoveTemp(Promise);return Future;
}
void UAetherNativePersistence::PollCheckpoint()
{
    if(!Checkpoint)return;Checkpoint->Poll();
    const auto Phase=Checkpoint->Phase();
    if(Phase!=EAetherWorldCheckpointPhase::Complete&&Phase!=EAetherWorldCheckpointPhase::Failed)return;
    auto Result=Checkpoint->Result();auto Promise=MoveTemp(CheckpointPromise);Checkpoint.Reset();
    // 先撤下运行中标志，再发布持久确认，回调可以安全安排下一次检查点。
    if(Result.Code!=EAetherStoreCode::Committed&&Result.Code!=EAetherStoreCode::Replayed)
        UE_LOG(LogTemp,Warning,TEXT("AETHER_NATIVE_CHECKPOINT_DEFERRED code=%d reason=%s"),int32(Result.Code),*Result.Detail);
    if(Result.World.IsSet()&&(Result.Code==EAetherStoreCode::Committed||Result.Code==EAetherStoreCode::Replayed)&&CheckpointPublished)CheckpointPublished(Result.World.GetValue());
    if(Promise)Promise->SetValue(MoveTemp(Result));
}
void UAetherNativePersistence::Fail(FString Reason)
{
    Detail=Reason.IsEmpty()?TEXT("Native persistence preparation failed"):MoveTemp(Reason);
    State=EAetherNativePersistencePhase::Failed;
    // 失败只冻结入口并保留磁盘，不清库、不覆盖来源、不转回 v9。
}
bool UAetherNativePersistence::IsTickable() const
{return !IsTemplate()&&(State==EAetherNativePersistencePhase::Inspecting||State==EAetherNativePersistencePhase::Auditing||State==EAetherNativePersistencePhase::Active||!Logins.IsEmpty()||Checkpoint.IsValid());}
TStatId UAetherNativePersistence::GetStatId() const{RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherNativePersistence,STATGROUP_Tickables);}
UWorld* UAetherNativePersistence::GetTickableGameObjectWorld() const{return GetWorld();}
void UAetherNativePersistence::StopScene()
{
    State=EAetherNativePersistencePhase::Stopped;DomainCapture={};CheckpointPublished={};
    if(auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>())Runtime->UninstallBackend();
    auto Pending=MoveTemp(Logins);for(auto& Job:Pending){FAetherStoreReadResult R;R.Code=EAetherStoreCode::Unavailable;Job->Result.SetValue(MoveTemp(R));}
    if(Checkpoint)Checkpoint->Stop();Checkpoint.Reset();
    auto PendingCheckpoint=MoveTemp(CheckpointPromise);
    Probe={};if(Bootstrap)Bootstrap->Stop();Bootstrap.Reset();
    // Runtime 同样持有 Store；关闭幂等，等待排队提交结束。不会在后台线程销毁 UObject。
    if(Store)Store->Close();Store.Reset();
    if(PendingCheckpoint){FAetherWorldCheckpointResult R;R.Code=EAetherStoreCode::Unavailable;R.Detail=TEXT("Shutdown drained writes; reload persisted world before retry");PendingCheckpoint->SetValue(MoveTemp(R));}
    State=EAetherNativePersistencePhase::Stopped;BoundScene.Reset();CheckpointElapsed=0;Prefix.Reset();Detail.Reset();
}

void UAetherNativePersistence::ReleaseScene(UWorld* Scene)
{
    check(IsInGameThread());if(BoundScene.Get()!=Scene)return;StopScene();State=EAetherNativePersistencePhase::Dormant;
}
void UAetherNativePersistence::Deinitialize(){StopScene();Super::Deinitialize();}
