#include "Misc/AutomationTest.h"
#include "Persistence/AetherNativePersistence.h"
#include "Persistence/AetherSqliteStore.h"
#include "Startup/AetherStartupSettings.h"
#include "Startup/AetherStartupClient.h"
#include "Networking/AetherCommandClient.h"
#include "Framework/AetherPlayerController.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherWorldCodec.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "Misc/ScopeExit.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
template<class T> TFuture<T> ReadyStartupResult(T Value)
{TPromise<T> Promise;auto Future=Promise.GetFuture();Promise.SetValue(MoveTemp(Value));return Future;}
// 控制旧写者的首个读取边界；Runtime/事实协调者仍执行真实收尾流程。
// 这是生命周期集成夹具，不是 SQLite 或实际地图 Travel 的耐久性测试。
class FStartupDrainStore final : public IAetherTransactionalStore
{
public:
    TMap<FAetherAggregateKey,FAetherStoredAggregate> Rows;
    TUniquePtr<TPromise<FAetherStoreSnapshotResult>> Held;
    FAetherStoreSnapshotQuery HeldQuery;
    int32 Writes=0,Closes=0;
    bool bHoldFirst=true,bClosed=false;
    FAetherStoreSnapshotResult Snapshot(const FAetherStoreSnapshotQuery& Query) const
    {
        FAetherStoreSnapshotResult R;R.Code=EAetherStoreCode::Found;R.ContainerCount=0;
        for(const auto& Key:Query.Keys)if(const auto* Row=Rows.Find(Key))R.Values.Add(Key,*Row);
        for(const auto& Row:Rows)if(Row.Key.Kind==EAetherAggregateKind::Profile)R.ProfileRevisions.Add(Row.Key.Id,Row.Value.Revision);
        return R;
    }
    void ReleaseRead(){if(Held){auto Promise=MoveTemp(Held);Promise->SetValue(Snapshot(HeldQuery));}}
    virtual TFuture<FAetherStoreSnapshotResult> ReadSnapshot(FAetherStoreSnapshotQuery Query) override
    {
        if(bHoldFirst){bHoldFirst=false;HeldQuery=MoveTemp(Query);Held=MakeUnique<TPromise<FAetherStoreSnapshotResult>>();return Held->GetFuture();}
        return ReadyStartupResult(Snapshot(Query));
    }
    virtual TFuture<FAetherStoreResult> Commit(FAetherTransaction Transaction) override
    {
        FAetherStoreResult R;if(bClosed)return ReadyStartupResult(MoveTemp(R));
        for(const auto& Write:Transaction.Writes)
            if(!Rows.Contains(Write.Value.Key)||Rows.FindChecked(Write.Value.Key).Revision!=Write.ExpectedRevision)
            {R.Code=EAetherStoreCode::Conflict;return ReadyStartupResult(MoveTemp(R));}
        for(const auto& Write:Transaction.Writes)Rows.Add(Write.Value.Key,Write.Value);
        ++Writes;R.Code=EAetherStoreCode::Committed;R.Result=Transaction.Result;R.FinalProfileRevision=Transaction.ExpectedProfileRevision+1;
        return ReadyStartupResult(MoveTemp(R));
    }
    virtual void Close() override {if(!bClosed){bClosed=true;++Closes;}}
    virtual ~FStartupDrainStore() override {ReleaseRead();Close();}
    virtual TFuture<FAetherStoreResult> LookupReceipt(FAetherReceiptQuery) override {return ReadyStartupResult(FAetherStoreResult());}
    virtual TFuture<FAetherStoreReadResult> Read(FAetherAggregateKey) override {return ReadyStartupResult(FAetherStoreReadResult());}
    virtual TFuture<FAetherStoreRevisionIndex> ReadRevisions(EAetherAggregateKind) override {return ReadyStartupResult(FAetherStoreRevisionIndex());}
    virtual TFuture<FAetherStoreEffectsResult> PendingEffects(FString) override {return ReadyStartupResult(FAetherStoreEffectsResult());}
    virtual TFuture<bool> AcknowledgeEffect(FString,FGuid) override {return ReadyStartupResult(false);}
    virtual TFuture<bool> Backup(FString) override {return ReadyStartupResult(false);}
};
// 仅延迟首个真实SQLite读取的交付，所有读写/事务/关闭仍交给真实Store。
class FStartupSqliteGate final : public IAetherTransactionalStore
{
public:
    explicit FStartupSqliteGate(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> In):Backing(MoveTemp(In)){}
    TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Backing;
    TUniquePtr<TPromise<FAetherStoreSnapshotResult>> Held;
    TFuture<FAetherStoreSnapshotResult> Source;
    bool bHoldFirst=true,bClosed=false;
    int32 Writes=0,Closes=0;
    bool ReleaseRead()
    {
        if(!Held)return true;if(!Source.IsReady())return false;
        auto Result=Source.Get();Source={};auto Promise=MoveTemp(Held);Promise->SetValue(MoveTemp(Result));return true;
    }
    virtual TFuture<FAetherStoreSnapshotResult> ReadSnapshot(FAetherStoreSnapshotQuery Query) override
    {
        if(bHoldFirst){bHoldFirst=false;Source=Backing->ReadSnapshot(MoveTemp(Query));Held=MakeUnique<TPromise<FAetherStoreSnapshotResult>>();return Held->GetFuture();}
        return Backing->ReadSnapshot(MoveTemp(Query));
    }
    virtual TFuture<FAetherStoreResult> Commit(FAetherTransaction T) override {++Writes;return Backing->Commit(MoveTemp(T));}
    virtual TFuture<FAetherStoreResult> LookupReceipt(FAetherReceiptQuery Q) override {return Backing->LookupReceipt(MoveTemp(Q));}
    virtual TFuture<FAetherStoreReadResult> Read(FAetherAggregateKey K) override {return Backing->Read(MoveTemp(K));}
    virtual TFuture<FAetherStoreRevisionIndex> ReadRevisions(EAetherAggregateKind K) override {return Backing->ReadRevisions(K);}
    virtual TFuture<FAetherStoreEffectsResult> PendingEffects(FString Id) override {return Backing->PendingEffects(MoveTemp(Id));}
    virtual TFuture<bool> AcknowledgeEffect(FString Id,FGuid Delivery) override {return Backing->AcknowledgeEffect(MoveTemp(Id),Delivery);}
    virtual TFuture<bool> Backup(FString Path) override {return Backing->Backup(MoveTemp(Path));}
    virtual void Close() override {if(!bClosed){bClosed=true;++Closes;Backing->Close();ReleaseRead();}}
    virtual ~FStartupSqliteGate() override {Close();}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherStartupLifecycleTest,"Aether.V10.Persistence.StartupWaitRetainsPriorWriter",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherStartupLifecycleTest::RunTest(const FString&)
{
    FString Why;const auto& D=FAetherV10Definitions::Get();
    if(!TestTrue(TEXT("Definitions ready"),D.bValid))return false;
    auto* Policy=NewObject<UAetherStartupSettings>();Policy->BackendDrainTimeoutSeconds=0;double Deadline=0;
    TestFalse(TEXT("Missing wait budget fails closed"),Policy->DrainDeadline(10,Deadline,Why));
    Policy->BackendDrainTimeoutSeconds=7;
    TestTrue(TEXT("Deadline derives from explicit policy"),Policy->DrainDeadline(10,Deadline,Why)&&Deadline==17);
    Policy->BackendDrainTimeoutSeconds=std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Unbounded waiting policy is rejected"),Policy->DrainDeadline(10,Deadline,Why));

    auto* GI=NewObject<UGameInstance>(GEngine);GI->AddToRoot();GI->InitializeStandalone(NAME_None,nullptr);
    auto* W=GI->GetWorld();auto* P=GI->GetSubsystem<UAetherNativePersistence>();auto* Runtime=GI->GetSubsystem<UAetherCommandRuntime>();
    auto Store=MakeShared<FStartupDrainStore,ESPMode::ThreadSafe>();
    ON_SCOPE_EXIT {Store->ReleaseRead();if(Runtime)for(int32 I=0;I<8&&Runtime->HasBackend();++I)Runtime->Tick(0);
        GI->Shutdown();if(W){GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}GI->RemoveFromRoot();};
    if(!TestNotNull(TEXT("Isolated world"),W)||!TestNotNull(TEXT("Persistence"),P)||!TestNotNull(TEXT("Runtime"),Runtime))return false;
    const FString Identity=TEXT("StartupDrainFixture");FAetherStoredAggregate Profile;
    if(!TestTrue(TEXT("Seed profile DTO"),AetherProfileBootstrap::BuildNew(Identity,Profile,Why)))return false;
    Store->Rows.Add(Profile.Key,Profile);
    FAetherWorldStateV10 Initial;Initial.RealmId=FGuid::NewGuid();FAetherStoredAggregate World;World.Key={EAetherAggregateKind::World,TEXT("Main")};
    if(!TestTrue(TEXT("Seed world DTO"),AetherWorldCodec::Encode(Initial,D.Items,D.Rules,{{Identity,0}},World.Payload,Why)))return false;
    Store->Rows.Add(World.Key,World);
    FAetherWorldStateV10 AuditedWorld;
    if(!TestTrue(TEXT("Audit controlled world's persisted value before activation"),
        AetherWorldCodec::Decode(World.Payload,D.Items,D.Rules,{{Identity,Profile.Revision}},AuditedWorld,Why)&&AuditedWorld.Revision==World.Revision))return false;
    const FAetherResolveConnectedContext Resolve=[](auto&,const auto&,const auto&,auto&){return false;};
    const FAetherPublishConnectedState Publish=[](auto&,const auto&,const auto*,const auto*){return true;};
    if(!TestTrue(TEXT("Install real previous runtime"),Runtime->InstallBackend(Store,AuditedWorld,Resolve,Publish,Why)))return false;
    const FString Prefix=TEXT("StartupWait_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TestFalse(TEXT("Active world is not silently stopped for new preparation"),P->Prepare(Prefix,true,Why));
    TestTrue(TEXT("Active conflict preserves runtime and unopened new store"),Runtime->IsInstalled()&&!P->Store);
    FAetherServerFact Fact;Fact.Kind=EAetherServerFactKind::EquipmentWear;Fact.CharacterId=Identity;Fact.FactId=TEXT("EquipmentWear");
    // 已离开库存的实例仍推进接受事件的游标；不向其他装备追索磨损。
    Fact.WornItems={FGuid::NewGuid()};
    if(!TestTrue(TEXT("Previous runtime accepts a durable fact"),Runtime->ObserveServerFact(Fact,Why)))return false;
    Runtime->UninstallBackend();Runtime->Tick(0);
    if(!TestTrue(TEXT("Previous runtime is draining a held read"),Runtime->HasBackend()&&!Runtime->IsInstalled()&&Store->Held.IsValid()))return false;
    if(!TestTrue(TEXT("New startup accepts waiting without opening a database"),P->Prepare(Prefix,true,Why)))return false;
    const FGuid FirstAttempt=P->StartupStatus().AttemptId;
    P->Tick(0);
    TestTrue(TEXT("Waiting retains previous writer ownership"),P->Phase()==EAetherNativePersistencePhase::WaitingForBackend&&!P->Store&&Runtime->HasBackend()&&Store->Closes==0);
    TestTrue(TEXT("Waiting has a concrete nonzero status"),P->StartupStatus().Stage==EAetherStartupStage::WaitingForBackend&&P->StartupStatus().Sequence>0);
    P->BackendDrainDeadline=FPlatformTime::Seconds()-1;P->Tick(0);P->Tick(0);
    TestTrue(TEXT("Timeout is terminal for only the new request"),P->Phase()==EAetherNativePersistencePhase::Failed&&P->StartupStatus().FailureCode==EAetherStartupFailure::BackendDrainTimedOut&&!P->Store);
    TestTrue(TEXT("Timeout does not close or discard accepted work"),Runtime->HasBackend()&&Store->Closes==0&&Store->Writes==0);
    TestFalse(TEXT("Stale cancel token cannot change the waiting generation"),P->CancelPreparation(W,FGuid::NewGuid()));
    TestTrue(TEXT("Exact cancellation terminates unopened request"),P->CancelPreparation(W,FirstAttempt));
    TestTrue(TEXT("Cancellation leaves old runtime draining without closing it"),Runtime->HasBackend()&&Store->Closes==0&&P->Phase()==EAetherNativePersistencePhase::Cancelled);
    if(!TestTrue(TEXT("Explicit new preparation has its own attempt"),P->Prepare(Prefix,true,Why)))return false;
    const FGuid SecondAttempt=P->StartupStatus().AttemptId;
    TestTrue(TEXT("New attempt differs"),SecondAttempt!=FirstAttempt);
    TestFalse(TEXT("Old cancel cannot cancel replacement"),P->CancelPreparation(W,FirstAttempt));
    TestTrue(TEXT("Current replacement can cancel"),P->CancelPreparation(W,SecondAttempt));
    Store->ReleaseRead();for(int32 I=0;I<8&&Runtime->HasBackend();++I)Runtime->Tick(0);
    TestFalse(TEXT("Real runtime eventually releases the previous backend"),Runtime->HasBackend());
    TestEqual(TEXT("Accepted fact still commits after new startup cancellation"),Store->Writes,1);
    TestEqual(TEXT("Previous owner closes its store exactly once"),Store->Closes,1);
    TestEqual(TEXT("Final world retains committed revision"),Store->Rows.FindChecked(World.Key).Revision,int64(1));
    TestEqual(TEXT("Final profile retains committed revision"),Store->Rows.FindChecked(Profile.Key).Revision,int64(1));
    P->Tick(0);TestTrue(TEXT("Draining completion does not restart a cancelled startup"),P->Phase()==EAetherNativePersistencePhase::Cancelled&&!P->Store);

    auto* Client=GI->GetSubsystem<UAetherStartupClient>();Client->Tick(0);
    const auto Token=Client->GetView().LocalAttemptToken;
    TestFalse(TEXT("Old start intent is rejected"),Client->RequestStart(FGuid::NewGuid()));
    TestFalse(TEXT("Old cancel intent is rejected"),Client->RequestCancel(FGuid::NewGuid()));
    TestFalse(TEXT("Old retry intent is rejected"),Client->RequestRetry(FGuid::NewGuid()));
    TestEqual(TEXT("Rejected intent cannot replace current local token"),Client->GetView().LocalAttemptToken,Token);
    int32 Changes=0;const auto Handle=Client->OnChanged.AddLambda([&]{++Changes;});
    Client->Tick(0);Client->Tick(0);
    TestEqual(TEXT("Unchanged Tick never interrupts a held UI click"),Changes,0);Client->OnChanged.Remove(Handle);
    FAetherStartupView Enabled;Enabled.LocalAttemptToken=FGuid::NewGuid();Enabled.Stage=EAetherStartupStage::WaitingForBackend;
    Enabled.bCanCancel=true;Enabled.CancelIssue=EAetherStartupRouteIssue::None;
    auto Disabled=Enabled;Disabled.bCanCancel=false;Disabled.CancelIssue=EAetherStartupRouteIssue::Busy;
    TestTrue(TEXT("Identical presentation is equal"),AetherStartup::SameView(Enabled,Enabled));
    TestFalse(TEXT("Capability disabling requires notification"),AetherStartup::SameView(Enabled,Disabled));
    TestFalse(TEXT("Capability restoring requires notification"),AetherStartup::SameView(Disabled,Enabled));

    // 当前LocalPlayer换PC但旧PC仍存活；分别走channel先到、startup先到的生产处理器。
    auto* Player=GI->CreateLocalPlayer(0,Why,false);
    if(!TestNotNull(TEXT("Local owner for controller lifecycle"),Player))return false;
    auto* FirstPC=W->SpawnActor<AAetherPlayerController>();auto* SecondPC=W->SpawnActor<AAetherPlayerController>();
    auto* ThirdPC=W->SpawnActor<AAetherPlayerController>();auto* FourthPC=W->SpawnActor<AAetherPlayerController>();
    if(!TestNotNull(TEXT("First controller"),FirstPC)||!TestNotNull(TEXT("Second controller"),SecondPC)||
        !TestNotNull(TEXT("Third controller"),ThirdPC)||!TestNotNull(TEXT("Fourth controller"),FourthPC))return false;
    FirstPC->SetPlayer(Player);
    if(!TestTrue(TEXT("First controller is the actual current local owner"),FirstPC->IsLocalController()&&Player->GetPlayerController(W)==FirstPC))return false;
    FAetherStartupSnapshot Status;Status.AttemptId=FGuid::NewGuid();Status.Sequence=1;Status.Stage=EAetherStartupStage::WorldReady;
    FirstPC->PublishStartupStatus(Status);
    const auto WorldStatus=P->StartupStatus();
    FirstPC->PublishStartupFailure(Status,EAetherStartupFailure::ProfileUnavailable);
    TestTrue(TEXT("Profile failure is a local terminal view"),Client->GetView().Stage==EAetherStartupStage::Failed&&Client->GetView().FailureCode==EAetherStartupFailure::ProfileUnavailable);
    TestTrue(TEXT("Individual profile failure never changes world persistence status"),P->StartupStatus().AttemptId==WorldStatus.AttemptId&&P->StartupStatus().Sequence==WorldStatus.Sequence);
    const FGuid FailedToken=Client->GetView().LocalAttemptToken;
    const FGuid SecondChannel=FGuid::NewGuid(),Realm=FGuid::NewGuid();
    SecondPC->SetPlayer(Player);SecondPC->ClientV10Channel(SecondChannel,Identity,Realm);
    TestTrue(TEXT("Channel-first new PC invalidates old failure without old EndPlay"),IsValid(FirstPC)&&Client->GetView().LocalAttemptToken!=FailedToken&&Client->GetView().Stage==EAetherStartupStage::Connecting);
    SecondPC->PublishStartupStatus(Status);
    TestTrue(TEXT("Startup arriving second preserves observed new channel"),Client->OwnsCommandChannel(SecondPC,SecondChannel));
    auto* Commands=Player->GetSubsystem<UAetherCommandClient>();
    if(!TestNotNull(TEXT("Local owner command client"),Commands))return false;
    const FGuid Transfer=FGuid::NewGuid();
    for(int32 Offset=0;Offset<Profile.Payload.Num();Offset+=AetherV10Network::ChunkBytes)
    {
        FAetherV10SnapshotChunk Chunk;Chunk.Channel=SecondChannel;Chunk.Transfer=Transfer;Chunk.Revision=0;
        Chunk.Total=Profile.Payload.Num();Chunk.Offset=Offset;Chunk.Checksum=FCrc::MemCrc32(Profile.Payload.GetData(),Profile.Payload.Num());
        Chunk.Bytes.Append(Profile.Payload.GetData()+Offset,FMath::Min(AetherV10Network::ChunkBytes,Profile.Payload.Num()-Offset));
        SecondPC->ClientV10Snapshot(Chunk);
    }
    TestTrue(TEXT("Old controller cache really contains a decoded profile"),Commands->GetProfile().IsSet()&&Commands->GetChannel()==SecondChannel);
    // 只设置夹具的已完成显示阶段；随后的PC失效与通道验证仍由生产Tick/处理器执行。
    const_cast<FAetherStartupView&>(Client->GetView()).Stage=EAetherStartupStage::Ready;
    ThirdPC->SetPlayer(Player);Client->Tick(0);
    TestTrue(TEXT("Controller replacement invalidates Ready before a new RPC"),Client->GetView().Stage!=EAetherStartupStage::Ready);
    TestFalse(TEXT("New PC cannot inherit old decoded command cache"),Client->OwnsCommandChannel(ThirdPC,Commands->GetChannel()));
    ThirdPC->PublishStartupStatus(Status);
    TestFalse(TEXT("Startup-first still waits for this PC's channel"),Client->OwnsCommandChannel(ThirdPC,SecondChannel));
    const FGuid ThirdChannel=FGuid::NewGuid();ThirdPC->ClientV10Channel(ThirdChannel,Identity,Realm);
    TestTrue(TEXT("Channel arriving second binds without wiping the startup identity"),Client->OwnsCommandChannel(ThirdPC,ThirdChannel)&&Client->GetView().ServerAttemptId==Status.AttemptId);
    const auto ThirdView=Client->GetView();Client->ReceiveStartup(FirstPC,Status);Client->ObserveCommandChannel(SecondPC,SecondChannel);
    TestTrue(TEXT("Still-valid old controllers cannot revive old state"),AetherStartup::SameView(ThirdView,Client->GetView()));

    for(const auto EndStage:{EAetherStartupStage::Ready,EAetherStartupStage::Failed})
    {
        auto* EndingPC=W->SpawnActor<AAetherPlayerController>();
        if(!TestNotNull(TEXT("Controller for destroyed-to-null interval"),EndingPC))return false;
        EndingPC->SetPlayer(Player);EndingPC->PublishStartupStatus(Status);
        if(EndStage==EAetherStartupStage::Failed)EndingPC->PublishStartupFailure(Status,EAetherStartupFailure::ProfileUnavailable);
        else const_cast<FAetherStartupView&>(Client->GetView()).Stage=EAetherStartupStage::Ready;
        const auto EndingToken=Client->GetView().LocalAttemptToken;
        TWeakObjectPtr<AAetherPlayerController> Ended=EndingPC;
        if(!TestTrue(TEXT("Destroy current controller before replacement exists"),EndingPC->Destroy()))return false;
        // 显式保留本LocalPlayer尚未接到替代PC的空档，不能让旧存活PC成为测试替身。
        Player->PlayerController=nullptr;
        if(!TestTrue(TEXT("Destroyed weak controller is genuinely stale in the null interval"),Ended.IsStale(true)&&Player->GetPlayerController(W)==nullptr))return false;
        Client->Tick(0);
        TestTrue(TEXT("Destroyed-to-null clears old terminal or Ready ownership"),Client->GetView().Stage==EAetherStartupStage::Connecting&&
            Client->GetView().LocalAttemptToken!=EndingToken&&!Client->GetView().ServerAttemptId.IsValid()&&Client->GetView().FailureCode==EAetherStartupFailure::None);
        const auto GapView=Client->GetView();Client->Tick(0);
        TestTrue(TEXT("Stable null interval does not repeatedly mint attempts"),AetherStartup::SameView(GapView,Client->GetView()));
    }
    auto* ReplacementPC=W->SpawnActor<AAetherPlayerController>();
    if(!TestNotNull(TEXT("Replacement after destroyed-to-null interval"),ReplacementPC))return false;
    ReplacementPC->SetPlayer(Player);ReplacementPC->PublishStartupStatus(Status);
    const FGuid ReplacementChannel=FGuid::NewGuid();ReplacementPC->ClientV10Channel(ReplacementChannel,Identity,Realm);
    TestTrue(TEXT("New controller recovers after null interval with only its own channel"),Client->GetView().Stage==EAetherStartupStage::WorldReady&&
        Client->GetView().ServerAttemptId==Status.AttemptId&&Client->OwnsCommandChannel(ReplacementPC,ReplacementChannel));
    auto CancelledStatus=Status;CancelledStatus.Sequence=2;CancelledStatus.Stage=EAetherStartupStage::Cancelled;
    ReplacementPC->PublishStartupStatus(CancelledStatus);const auto CancelledToken=Client->GetView().LocalAttemptToken;
    FourthPC->SetPlayer(Player);Client->Tick(0);FourthPC->PublishStartupStatus(Status);FourthPC->ClientV10Channel(FGuid::NewGuid(),Identity,Realm);
    TestTrue(TEXT("Explicit cancelled attempt cannot revive through PC replacement"),Client->GetView().Stage==EAetherStartupStage::Cancelled&&Client->GetView().LocalAttemptToken==CancelledToken);

    // 成功路径使用真实隔离SQLite文件，必须等旧Runtime提交/关闭后新Prepare才读到最终版本。
    const FString SqlitePrefix=TEXT("StartupSqlite_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FAetherSqliteOptions Options;Options.DatabasePath=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("V10State")/SqlitePrefix/TEXT("state.sqlite"));
    auto Open=AetherSQLite::Open(Options);if(!TestTrue(TEXT("Open isolated real startup database"),Open.Store.IsValid()))return false;
    if(!TestTrue(TEXT("Seed real world"),Open.Store->InitializeWorld(World).Get().Code==EAetherStoreCode::Committed)||
        !TestTrue(TEXT("Seed real profile"),Open.Store->CreateProfile(Profile).Get().Code==EAetherStoreCode::Found)){Open.Store->Close();return false;}
    auto Gate=MakeShared<FStartupSqliteGate,ESPMode::ThreadSafe>(Open.Store.ToSharedRef());
    ON_SCOPE_EXIT {Gate->Close();};
    FAetherStoreSnapshotQuery AuditQuery;AuditQuery.Keys={World.Key};AuditQuery.bIncludeProfileRevisions=true;
    const auto Audit=Open.Store->ReadSnapshot(MoveTemp(AuditQuery)).Get();const auto* AuditedRow=Audit.Values.Find(World.Key);
    FAetherWorldStateV10 AuditedSqliteWorld;
    if(!TestTrue(TEXT("Audit real database before installing old backend"),Audit.Code==EAetherStoreCode::Found&&AuditedRow&&
        AetherWorldCodec::Decode(AuditedRow->Payload,D.Items,D.Rules,Audit.ProfileRevisions,AuditedSqliteWorld,Why)&&AuditedSqliteWorld.Revision==AuditedRow->Revision))return false;
    if(!TestTrue(TEXT("Install old real-database backend"),Runtime->InstallBackend(Gate,AuditedSqliteWorld,Resolve,Publish,Why)))return false;
    if(!TestTrue(TEXT("Accept fact before real-database shutdown"),Runtime->ObserveServerFact(Fact,Why)))return false;
    Runtime->UninstallBackend();Runtime->Tick(0);
    if(!TestTrue(TEXT("Hold accepted real-database work at its read boundary"),Gate->Held.IsValid()))return false;
    if(!TestTrue(TEXT("Preparation waits to reopen the same database"),P->Prepare(SqlitePrefix,false,Why)))return false;
    P->Tick(0);TestFalse(TEXT("Real new connection is not opened while old writer owns backend"),P->Store.IsValid());
    IAetherTransactionalStore* OpenedOnce=nullptr;const double Until=FPlatformTime::Seconds()+5;
    while(P->Phase()!=EAetherNativePersistencePhase::Prepared&&P->Phase()!=EAetherNativePersistencePhase::Failed&&FPlatformTime::Seconds()<Until)
    {
        Gate->ReleaseRead();Runtime->Tick(0);P->Tick(0);
        if(P->Store)
        {
            if(!OpenedOnce)OpenedOnce=P->Store.Get();
            TestTrue(TEXT("Startup retains one opened store while audit advances"),OpenedOnce==P->Store.Get());
        }
        FPlatformProcess::Sleep(.001f);
    }
    TestTrue(TEXT("Drain then real audit reaches Prepared"),P->Phase()==EAetherNativePersistencePhase::Prepared);
    TestEqual(TEXT("Old real writer commits exactly one accepted fact"),Gate->Writes,1);
    TestEqual(TEXT("Old real writer closes once before replacement"),Gate->Closes,1);
    if(P->Bootstrap&&P->Bootstrap->World().IsSet())
        TestEqual(TEXT("New audit reads the old writer's committed world revision"),P->Bootstrap->World()->Revision,int64(1));
    else AddError(TEXT("Expected audited persisted world after normal drain"));
    if(P->Store)
    {
        const auto Row=P->Store->Read(Profile.Key).Get();FAetherProfileStateV10 Restored;
        TestTrue(TEXT("New connection sees committed profile and wear cursor"),Row.Value.IsSet()&&Row.Value->Revision==1&&
            AetherProfileCodec::Decode(Row.Value->Payload,D.Items,D.Skills,D.Rules,Restored,Why)&&Restored.WearSequence==1);
        P->Tick(0);P->Tick(0);TestTrue(TEXT("Further Prepared ticks never reopen the store"),P->Store.Get()==OpenedOnce);
    }
    P->ReleaseScene(W);
    return true;
}
#endif
