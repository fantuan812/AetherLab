#include "Misc/AutomationTest.h"
#include "Persistence/AetherNativePersistence.h"
#include "World/AetherWorldCodec.h"
#include "World/AetherContainerCodec.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
template<class T> TFuture<T> ReadyActivationResult(T Value)
{TPromise<T> Promise;auto Future=Promise.GetFuture();Promise.SetValue(MoveTemp(Value));return Future;}

// 只提供确定性审计数据；Bootstrap 审计、Activate 和 CommandRuntime 安装/卸载走生产实现。
// 没有排队的事务或磁盘写入，不能作为 SQLite 耐久性或在途事务排空的证据。
class FActivationLifecycleStore final : public IAetherTransactionalStore
{
public:
    TMap<FAetherAggregateKey,FAetherStoredAggregate> Rows;
    FGuid Realm;
    FString Owner,Container;
    int32 Closes=0;
    bool bClosed=false;
    virtual TFuture<FAetherStoreSnapshotResult> ReadSnapshot(FAetherStoreSnapshotQuery Query) override
    {
        FAetherStoreSnapshotResult R;
        if(!bClosed)
        {
            R.Code=EAetherStoreCode::Found;R.ContainerCount=0;
            for(const auto& Key:Query.Keys)if(const auto* Row=Rows.Find(Key))R.Values.Add(Key,*Row);
            for(const auto& Entry:Rows)
                if(Entry.Key.Kind==EAetherAggregateKind::Profile)R.ProfileRevisions.Add(Entry.Key.Id,Entry.Value.Revision);
                else if(Entry.Key.Kind==EAetherAggregateKind::Container){R.ContainerRevisions.Add(Entry.Key.Id,Entry.Value.Revision);++R.ContainerCount;}
                else if(Entry.Key.Kind==EAetherAggregateKind::World)R.WorldRevisions.Add(Entry.Key.Id,Entry.Value.Revision);
        }
        return ReadyActivationResult(MoveTemp(R));
    }
    virtual TFuture<FAetherStoreReadResult> Read(FAetherAggregateKey Key) override
    {
        FAetherStoreReadResult R;
        if(!bClosed){R.Code=EAetherStoreCode::Missing;if(const auto* Row=Rows.Find(Key)){R.Code=EAetherStoreCode::Found;R.Value=*Row;}}
        return ReadyActivationResult(MoveTemp(R));
    }
    virtual void Close() override {if(!bClosed){bClosed=true;++Closes;}}
    virtual ~FActivationLifecycleStore() override {Close();}
    virtual TFuture<FAetherStoreResult> Commit(FAetherTransaction) override {return ReadyActivationResult(FAetherStoreResult());}
    virtual TFuture<FAetherStoreResult> LookupReceipt(FAetherReceiptQuery) override {return ReadyActivationResult(FAetherStoreResult());}
    virtual TFuture<FAetherStoreRevisionIndex> ReadRevisions(EAetherAggregateKind) override {return ReadyActivationResult(FAetherStoreRevisionIndex());}
    virtual TFuture<FAetherStoreEffectsResult> PendingEffects(FString) override {return ReadyActivationResult(FAetherStoreEffectsResult());}
    virtual TFuture<bool> AcknowledgeEffect(FString,FGuid) override {return ReadyActivationResult(false);}
    virtual TFuture<bool> Backup(FString) override {return ReadyActivationResult(false);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNativeActivationLifecycleTest,"Aether.V10.Persistence.RestoreSceneReentrancy",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNativeActivationLifecycleTest::RunTest(const FString&)
{
    const auto& D=FAetherV10Definitions::Get();FString Why;
    if(!TestTrue(TEXT("Definitions ready"),D.bValid))return false;
    const auto MakeStore=[&](const FString& Suffix)
    {
        auto Store=MakeShared<FActivationLifecycleStore,ESPMode::ThreadSafe>();Store->Realm=FGuid::NewGuid();
        Store->Owner=TEXT("Activation")+Suffix;Store->Container=TEXT("Activation.")+Suffix;
        FAetherStoredAggregate Profile;
        if(!TestTrue(TEXT("Encode profile fixture"),AetherProfileBootstrap::BuildNew(Store->Owner,Profile,Why)))return Store;
        Store->Rows.Add(Profile.Key,Profile);
        FAetherWorldStateV10 World;World.RealmId=Store->Realm;
        FAetherStoredAggregate WorldRow;WorldRow.Key={EAetherAggregateKind::World,TEXT("Main")};
        if(!TestTrue(TEXT("Encode world fixture"),AetherWorldCodec::Encode(World,D.Items,D.Rules,{{Store->Owner,0}},WorldRow.Payload,Why)))return Store;
        Store->Rows.Add(WorldRow.Key,WorldRow);
        FAetherContainerStateV10 Container;Container.ContainerId=Store->Container;Container.Inventory.Capacity=D.Items.DefaultCapacity;
        FAetherStoredAggregate ContainerRow;ContainerRow.Key={EAetherAggregateKind::Container,Store->Container};
        if(TestTrue(TEXT("Encode container fixture"),AetherContainerCodec::Encode(Container,D.Items,ContainerRow.Payload,Why)))Store->Rows.Add(ContainerRow.Key,ContainerRow);
        return Store;
    };
    auto* GI=NewObject<UGameInstance>(GEngine);GI->AddToRoot();GI->InitializeStandalone(NAME_None,nullptr);
    auto* W=GI->GetWorld();auto* P=GI->GetSubsystem<UAetherNativePersistence>();auto* Runtime=GI->GetSubsystem<UAetherCommandRuntime>();
    ON_SCOPE_EXIT {GI->Shutdown();if(W){GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}GI->RemoveFromRoot();};
    if(!TestNotNull(TEXT("Isolated scene"),W)||!TestNotNull(TEXT("Persistence subsystem"),P)||!TestNotNull(TEXT("Production command runtime"),Runtime))return false;
    const auto PrepareFixture=[&](const TSharedRef<FActivationLifecycleStore,ESPMode::ThreadSafe>& Store)
    {
        // 仅绕开 Prepare 的真实数据库打开；完整 DTO 审计由生产 Bootstrap/Tick 完成。
        P->Store=Store;P->BoundScene=W;P->SceneGeneration=FGuid::NewGuid();P->State=EAetherNativePersistencePhase::Auditing;
        P->Bootstrap=MakeUnique<FAetherWorldBootstrap>(Store);FString Reason;
        if(!P->Bootstrap->Start(false,Reason))return false;
        for(int32 Step=0;Step<8&&P->Phase()==EAetherNativePersistencePhase::Auditing;++Step)P->Tick(0);
        return P->Phase()==EAetherNativePersistencePhase::Prepared;
    };
    const auto CheckArguments=[&](const auto& Store,const auto& World,const auto& Profiles,const auto& Containers)
    {
        TestEqual(TEXT("Restore keeps original world after scene release"),World.RealmId,Store->Realm);
        TestTrue(TEXT("Restore keeps original profile index after scene release"),Profiles.Num()==1&&Profiles.Contains(Store->Owner)&&Profiles.FindChecked(Store->Owner)==0);
        TestTrue(TEXT("Restore keeps original container descriptor after scene release"),Containers.Num()==1&&Containers[0].Id==Store->Container);
    };
    const FAetherResolveConnectedContext Resolve=[](auto&,const auto&,const auto&,auto&){return false;};
    const FAetherPublishConnectedState Publish=[](auto&,const auto&,const auto*,const auto*){return true;};

    auto First=MakeStore(TEXT("First"));if(!TestTrue(TEXT("First scene passes real audit"),PrepareFixture(First)))return false;
    TestFalse(TEXT("A successful old restore cannot activate after exit"),P->Activate(Resolve,Publish,
        [&](const auto& World,const auto& Profiles,const auto& Containers,FString&)
        {P->ReleaseScene(W);CheckArguments(First,World,Profiles,Containers);return true;},Why));
    TestTrue(TEXT("Exited scene remains dormant"),P->Phase()==EAetherNativePersistencePhase::Dormant);
    TestFalse(TEXT("Old success never installs a backend"),Runtime->HasBackend());
    TestEqual(TEXT("Exited original store closes exactly once"),First->Closes,1);

    auto Second=MakeStore(TEXT("Second")),Replacement=MakeStore(TEXT("Replacement"));
    if(!TestTrue(TEXT("Second scene passes real audit"),PrepareFixture(Second)))return false;
    bool bReplacementPrepared=false,bNestedRestore=false;
    TestFalse(TEXT("An old restore failure cannot poison a replacement"),P->Activate(Resolve,Publish,
        [&](const auto& World,const auto& Profiles,const auto& Containers,FString& Reason)
        {
            P->ReleaseScene(W);bReplacementPrepared=PrepareFixture(Replacement);
            CheckArguments(Second,World,Profiles,Containers);
            FString NestedReason;
            TestFalse(TEXT("Nested activation waits for the old callback to unwind"),P->Activate(Resolve,Publish,
                [&](const auto&,const auto&,const auto&,FString&){bNestedRestore=true;return true;},NestedReason));
            Reason=TEXT("Old restore rejected");return false;
        },Why));
    TestTrue(TEXT("Replacement audit is retained"),bReplacementPrepared&&P->Phase()==EAetherNativePersistencePhase::Prepared);
    TestTrue(TEXT("Old failure cannot overwrite replacement diagnostic"),P->Failure().IsEmpty());
    TestFalse(TEXT("Nested restore was not invoked"),bNestedRestore);
    TestFalse(TEXT("Old failure never installs replacement store with old adapters"),Runtime->HasBackend());
    TestFalse(TEXT("Old callback does not close replacement store"),Replacement->bClosed);
    if(!TestTrue(TEXT("Replacement activates after old callback returns"),P->Activate(Resolve,Publish,
        [&](const auto& World,const auto& Profiles,const auto& Containers,FString&)
        {CheckArguments(Replacement,World,Profiles,Containers);return true;},Why)))return false;
    TestTrue(TEXT("Only current scene is active"),P->Phase()==EAetherNativePersistencePhase::Active&&Runtime->IsInstalled());
    P->ReleaseScene(W);TestFalse(TEXT("Real empty command backend is uninstalled"),Runtime->HasBackend());
    TestEqual(TEXT("Current backend owns one store close"),Replacement->Closes,1);

    auto Rejected=MakeStore(TEXT("Rejected"));if(!TestTrue(TEXT("Rejected scene passes real audit"),PrepareFixture(Rejected)))return false;
    TestFalse(TEXT("Same-scene restore rejection remains a real failure"),P->Activate(Resolve,Publish,
        [](const auto&,const auto&,const auto&,FString& Reason){Reason=TEXT("Restore rejected");return false;},Why));
    TestTrue(TEXT("Same-scene failure is retained"),P->Phase()==EAetherNativePersistencePhase::Failed&&P->Failure()==TEXT("Restore rejected"));
    TestFalse(TEXT("Rejected scene never installs backend"),Runtime->HasBackend());
    P->ReleaseScene(W);return true;
}
#endif
