#include "Misc/AutomationTest.h"
#include "Persistence/AetherNativePersistence.h"
#include "Startup/AetherStartupSettings.h"
#include "Startup/AetherStartupClient.h"
#include "Framework/AetherPlayerController.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherWorldCodec.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
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
    const FAetherResolveConnectedContext Resolve=[](auto&,const auto&,const auto&,auto&){return false;};
    const FAetherPublishConnectedState Publish=[](auto&,const auto&,const auto*,const auto*){return true;};
    if(!TestTrue(TEXT("Install real previous runtime"),Runtime->InstallBackend(Store,Resolve,Publish,Why)))return false;
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
    return true;
}
#endif
