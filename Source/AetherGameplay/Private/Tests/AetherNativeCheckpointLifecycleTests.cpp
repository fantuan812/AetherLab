#include "Misc/AutomationTest.h"
#include "Persistence/AetherNativePersistence.h"
#include "Definitions/AetherV10Definitions.h"
#include "World/AetherWorldCodec.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
template<class T> TFuture<T> ReadyCheckpointResult(T Value)
{TPromise<T> Promise;auto Future=Promise.GetFuture();Promise.SetValue(MoveTemp(Value));return Future;}

// 确定性存储替身只控制 Future 边界，不替代 SQLite 耐久性测试。
// 场景、SaveWorldMutation、PollCheckpoint、ReleaseScene 和编解码均走生产代码。
class FCheckpointLifecycleStore final : public IAetherTransactionalStore
{
public:
    FAetherStoredAggregate Row;
    int32 Writes=0,Closes=0;
    bool bDeferWrite=false,bClosed=false;
    TFunction<void()> OnClose;
    TOptional<FAetherStoredAggregate> QueuedWrite;
    TUniquePtr<TPromise<FAetherStoreReadResult>> WritePromise;
    virtual TFuture<FAetherStoreSnapshotResult> ReadSnapshot(FAetherStoreSnapshotQuery) override
    {
        FAetherStoreSnapshotResult R;
        if(!bClosed){R.Code=EAetherStoreCode::Found;R.Values.Add(Row.Key,Row);}
        return ReadyCheckpointResult(MoveTemp(R));
    }
    virtual TFuture<FAetherStoreReadResult> CompareExchangeWorld(FAetherAggregateWrite Write) override
    {
        FAetherStoreReadResult R;
        if(bClosed)return ReadyCheckpointResult(MoveTemp(R));
        ++Writes;
        if(Write.ExpectedRevision!=Row.Revision){R.Code=EAetherStoreCode::Conflict;return ReadyCheckpointResult(MoveTemp(R));}
        if(bDeferWrite)
        {QueuedWrite=MoveTemp(Write.Value);WritePromise=MakeUnique<TPromise<FAetherStoreReadResult>>();return WritePromise->GetFuture();}
        Row=MoveTemp(Write.Value);R.Code=EAetherStoreCode::Committed;R.Value=Row;return ReadyCheckpointResult(MoveTemp(R));
    }
    virtual void Close() override
    {
        if(bClosed)return;bClosed=true;++Closes;
        if(WritePromise)
        {
            Row=MoveTemp(QueuedWrite.GetValue());QueuedWrite.Reset();FAetherStoreReadResult R;
            R.Code=EAetherStoreCode::Committed;R.Value=Row;auto Promise=MoveTemp(WritePromise);Promise->SetValue(MoveTemp(R));
        }
        const auto Callback=OnClose;if(Callback)Callback();
    }
    virtual ~FCheckpointLifecycleStore() override {Close();}
    virtual TFuture<FAetherStoreResult> Commit(FAetherTransaction) override {return ReadyCheckpointResult(FAetherStoreResult());}
    virtual TFuture<FAetherStoreResult> LookupReceipt(FAetherReceiptQuery) override {return ReadyCheckpointResult(FAetherStoreResult());}
    virtual TFuture<FAetherStoreReadResult> Read(FAetherAggregateKey) override {return ReadyCheckpointResult(FAetherStoreReadResult());}
    virtual TFuture<FAetherStoreRevisionIndex> ReadRevisions(EAetherAggregateKind) override {return ReadyCheckpointResult(FAetherStoreRevisionIndex());}
    virtual TFuture<FAetherStoreEffectsResult> PendingEffects(FString) override {return ReadyCheckpointResult(FAetherStoreEffectsResult());}
    virtual TFuture<bool> AcknowledgeEffect(FString,FGuid) override {return ReadyCheckpointResult(false);}
    virtual TFuture<bool> Backup(FString) override {return ReadyCheckpointResult(false);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNativeCheckpointLifecycleTest,"Aether.V10.Persistence.CheckpointSceneReentrancy",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNativeCheckpointLifecycleTest::RunTest(const FString&)
{
    const auto& D=FAetherV10Definitions::Get();FString Why;
    if(!TestTrue(TEXT("Definitions ready"),D.bValid))return false;
    FAetherWorldStateV10 InitialWorld;InitialWorld.RealmId=FGuid::NewGuid();
    FAetherStoredAggregate Initial;Initial.Key={EAetherAggregateKind::World,TEXT("Main")};
    if(!TestTrue(TEXT("Encode audited fixture world"),AetherWorldCodec::Encode(InitialWorld,D.Items,D.Rules,{},Initial.Payload,Why)))return false;

    auto* GI=NewObject<UGameInstance>(GEngine);GI->AddToRoot();GI->InitializeStandalone(NAME_None,nullptr);
    auto* W=GI->GetWorld();auto* P=GI->GetSubsystem<UAetherNativePersistence>();
    ON_SCOPE_EXIT {GI->Shutdown();if(W){GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}GI->RemoveFromRoot();};
    if(!TestNotNull(TEXT("Isolated scene"),W)||!TestNotNull(TEXT("Production persistence subsystem"),P))return false;
    // 注入已审计的存储/场景，以隔离启动装配；以下请求、轮询与退出没有测试替代实现。
    const auto Attach=[&](const TSharedRef<FCheckpointLifecycleStore,ESPMode::ThreadSafe>& Store)
    {
        P->Store=Store;P->BoundScene=W;P->SceneGeneration=FGuid::NewGuid();P->State=EAetherNativePersistencePhase::Active;
    };
    const auto MakeStore=[&]
    {auto Store=MakeShared<FCheckpointLifecycleStore,ESPMode::ThreadSafe>();Store->Row=Initial;return Store;};
    const auto CopyWorld=[](const auto& Before,auto& After,FString&){After=Before;return true;};

    auto First=MakeStore(),Second=MakeStore();Attach(First);
    int32 OldPublications=0,NewPublications=0,OldMutations=0;
    bool bOldAliveInsideCapture=false,bCloseReentered=false;
    TWeakPtr<FAetherWorldCheckpoint> OldCheckpoint;
    TFuture<FAetherWorldCheckpointResult> Replacement;
    First->OnClose=[&]
    {
        bCloseReentered=true;P->ReleaseScene(W);
        FString Reason;TestFalse(TEXT("Reentrant preparation cannot reopen a draining scene"),P->Prepare(TEXT("CheckpointLifecycle"),true,Reason));
        TestTrue(TEXT("Teardown remains stopped during reentrant release"),P->Phase()==EAetherNativePersistencePhase::Stopped);
    };
    P->ConfigureCheckpoints([&](const auto& Before,auto& After,FString&)
    {
        After=Before;P->ReleaseScene(W);bOldAliveInsideCapture=OldCheckpoint.IsValid();
        Attach(Second);P->ConfigureCheckpoints(CopyWorld,[&](const auto&){++NewPublications;});
        Replacement=P->SaveLoadedPhysics();return true;
    },[&](const auto&){++OldPublications;});
    auto Cancelled=P->SaveWorldMutation([&](const auto&,auto&,FString&){++OldMutations;return true;});
    OldCheckpoint=P->Checkpoint;P->PollCheckpoint();
    TestTrue(TEXT("Capture exit retains its executing checkpoint"),bOldAliveInsideCapture);
    TestTrue(TEXT("Close reentry exercised"),bCloseReentered);
    TestFalse(TEXT("Old checkpoint released after its poll returns"),OldCheckpoint.IsValid());
    TestEqual(TEXT("Stopped capture cannot execute old mutation"),OldMutations,0);
    TestEqual(TEXT("Stopped capture cannot submit a write"),First->Writes,0);
    TestEqual(TEXT("Stopped capture cannot publish"),OldPublications,0);
    if(TestTrue(TEXT("Stopped request receives a terminal result"),Cancelled.IsReady()))
        TestTrue(TEXT("Stopped request does not claim a rollback or success"),Cancelled.Get().Code==EAetherStoreCode::Unavailable);
    if(!TestTrue(TEXT("Replacement request remains pending after old poll"),Replacement.IsValid()&&!Replacement.IsReady()&&P->IsSavingWorld()))return false;
    P->PollCheckpoint();
    if(TestTrue(TEXT("Replacement completes on its own poll"),Replacement.IsReady()))
        TestTrue(TEXT("Replacement confirms its own commit"),Replacement.Get().Code==EAetherStoreCode::Committed);
    TestEqual(TEXT("New scene publishes exactly once"),NewPublications,1);
    P->ReleaseScene(W);P->ReleaseScene(W);TestEqual(TEXT("Repeated exit closes store once"),Second->Closes,1);

    auto MutationStore=MakeStore(),AfterMutation=MakeStore();Attach(MutationStore);
    int32 MutationPublications=0;
    P->ConfigureCheckpoints(CopyWorld,[&](const auto&){++MutationPublications;});
    auto MutationExit=P->SaveWorldMutation([&](const auto&,auto&,FString&)
    {
        P->ReleaseScene(W);Attach(AfterMutation);P->ConfigureCheckpoints(CopyWorld,{});
        Replacement=P->SaveLoadedPhysics();return true;
    });
    P->PollCheckpoint();
    TestEqual(TEXT("Mutation exit is stopped before compare-exchange"),MutationStore->Writes,0);
    TestEqual(TEXT("Mutation exit never publishes old candidate"),MutationPublications,0);
    if(TestTrue(TEXT("Mutation exit completes old promise"),MutationExit.IsReady()))
        TestTrue(TEXT("Mutation exit does not claim success"),MutationExit.Get().Code==EAetherStoreCode::Unavailable);
    if(!TestTrue(TEXT("Mutation exit cannot consume replacement promise"),Replacement.IsValid()&&!Replacement.IsReady()&&P->IsSavingWorld()))return false;
    P->PollCheckpoint();
    if(TestTrue(TEXT("Post-mutation replacement completes independently"),Replacement.IsReady()))
        TestTrue(TEXT("Post-mutation replacement confirms its own commit"),Replacement.Get().Code==EAetherStoreCode::Committed);
    P->ReleaseScene(W);

    auto Third=MakeStore(),Fourth=MakeStore();Attach(Third);
    auto Token=MakeShared<int32>(17);TWeakPtr<int32> WeakToken=Token;
    bool bPublisherAliveAfterExit=false;int32 FourthPublications=0;
    P->ConfigureCheckpoints(CopyWorld,[&,Token](const auto&)
    {
        P->ReleaseScene(W);bPublisherAliveAfterExit=WeakToken.IsValid()&&*Token==17;
        Attach(Fourth);P->ConfigureCheckpoints(CopyWorld,[&](const auto&){++FourthPublications;});
        Replacement=P->SaveLoadedPhysics();
    });
    Token.Reset();auto Published=P->SaveLoadedPhysics();P->PollCheckpoint();
    TestTrue(TEXT("Publisher survives clearing its own member callback"),bPublisherAliveAfterExit);
    TestFalse(TEXT("Publisher captures release after return"),WeakToken.IsValid());
    if(TestTrue(TEXT("Published request still confirms durable success"),Published.IsReady()))
        TestTrue(TEXT("Exit after publication does not rewrite durable outcome"),Published.Get().Code==EAetherStoreCode::Committed);
    if(!TestTrue(TEXT("Old publication preserves new pending request"),Replacement.IsValid()&&!Replacement.IsReady()&&P->IsSavingWorld()))return false;
    P->PollCheckpoint();TestEqual(TEXT("Old result never calls replacement publisher"),FourthPublications,1);
    if(TestTrue(TEXT("Replacement future was not stolen by old publication"),Replacement.IsReady()))Replacement.Get();
    P->ReleaseScene(W);

    auto Deferred=MakeStore();Deferred->bDeferWrite=true;Attach(Deferred);int32 LatePublications=0;
    P->ConfigureCheckpoints(CopyWorld,[&](const auto&){++LatePublications;});
    auto Uncertain=P->SaveLoadedPhysics();P->PollCheckpoint();
    if(!TestTrue(TEXT("Write is queued before scene exit"),Deferred->Writes==1&&!Uncertain.IsReady()&&P->IsSavingWorld()))return false;
    P->ReleaseScene(W);
    TestEqual(TEXT("Close drains accepted write despite cancelled observer"),Deferred->Row.Revision,int64(1));
    TestEqual(TEXT("Late committed write never publishes into exited scene"),LatePublications,0);
    if(TestTrue(TEXT("Exited pending request completes"),Uncertain.IsReady()))
        TestTrue(TEXT("Pending exit requires reload rather than claiming rollback"),Uncertain.Get().Code==EAetherStoreCode::Unavailable);
    auto Reopened=MakeStore();Reopened->Row=Deferred->Row;Attach(Reopened);int64 RestoredRevision=-1;
    P->ConfigureCheckpoints([&](const auto& Before,auto& After,FString&){RestoredRevision=Before.Revision;After=Before;return true;},{});
    auto Restored=P->SaveLoadedPhysics();P->PollCheckpoint();
    TestEqual(TEXT("New scene captures from drained committed revision"),RestoredRevision,int64(1));
    if(TestTrue(TEXT("Restored request completes"),Restored.IsReady()))
    {const auto R=Restored.Get();TestTrue(TEXT("Restored request advances latest revision once"),R.Code==EAetherStoreCode::Committed&&R.World.IsSet()&&R.World->Revision==2);}
    P->ReleaseScene(W);return true;
}
#endif
