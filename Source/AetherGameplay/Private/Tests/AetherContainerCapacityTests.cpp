#include "Misc/AutomationTest.h"
#include "../Persistence/AetherSqliteInternal.h"
#include "World/AetherContainerCodec.h"
#include "World/AetherDropSlots.h"
#include "Framework/AetherFrontierMode.h"
#include "Definitions/AetherV10Definitions.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherContainerCapacityTest,"Aether.Systems.Persistence.ContainerCapacityInterleaving",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherContainerCapacityTest::RunTest(const FString&)
{
    using namespace AetherSQLite::Private;
    const auto& D=FAetherV10Definitions::Get();FString Why;
    if(!TestTrue(TEXT("Load container definitions"),D.bValid))return false;
    FAetherSqliteOptions Options;Options.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/ContainerCapacity")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    auto Opened=AetherSQLite::Open(Options);if(!TestTrue(TEXT("Open isolated fixture"),Opened.Store.IsValid()))return false;
    // Opaque profile/world rows are sufficient for this storage-boundary test;
    // container rows are real encoded DTOs, including all tombstones.
    FAetherTransaction Seed;Seed.ActorId=TEXT("CapacityAlice");Seed.CommandId=AetherTransactions::NewCommandId(-1);Seed.Request={1};
    FAetherAggregateWrite P;P.Value.Key={EAetherAggregateKind::Profile,Seed.ActorId};P.Value.Payload={1};Seed.Writes.Add(P);
    P.Value.Key={EAetherAggregateKind::World,TEXT("Main")};Seed.Writes.Add(P);
    if(!TestTrue(TEXT("Seed world and owner"),Opened.Store->Commit(Seed).Get().Code==EAetherStoreCode::Committed))return false;
    Opened.Store->Close();Opened.Store.Reset();
    sqlite3* DB=nullptr;FTCHARToUTF8 Path(*Options.DatabasePath);
    if(!TestEqual(TEXT("Open fixture writer"),sqlite3_open(Path.Get(),&DB),SQLITE_OK))return false;
    ON_SCOPE_EXIT {sqlite3_close(DB);};
    const auto Fill=[&](int32 Begin,int32 End)
    {
        FTransactionGuard Tx(DB);if(!Tx.Active)return false;
        for(int32 I=Begin;I<End;++I)
        {
            FAetherContainerStateV10 C;C.ContainerId=FString::Printf(TEXT("Drop.Legacy%d"),I);C.Kind=EAetherContainerKind::WorldDrop;C.bActive=false;C.Inventory.Capacity=1;
            TArray<uint8> Payload;if(!AetherContainerCodec::Encode(C,D.Items,Payload,Why))return false;
            FStatement Insert(DB,"INSERT INTO aggregates(kind,id,revision,schema,payload) VALUES(2,?,0,10,?)");
            if(!Insert.Text(1,C.ContainerId)||!Insert.Blob(2,Payload)||Insert.Step()!=SQLITE_DONE)return false;
        }
        return Tx.Commit();
    };
    if(!TestTrue(TEXT("Fill tombstones directly, without thousands of gameplay loops"),Fill(0,AetherContainerLimits::DropAdmission-1)))return false;
    FAetherStoreSnapshotQuery Query;Query.bIncludeContainerCount=true;
    TestEqual(TEXT("Drop snapshot sees last admitted slot"),ReadSnapshot(DB,Query).ContainerCount,AetherContainerLimits::DropAdmission-1);
    FAetherContainerStateV10 Storage;Storage.ContainerId=TEXT("Storage_CapacityAlice");Storage.OwnerCharacterId=Seed.ActorId;Storage.Kind=EAetherContainerKind::PersonalStorage;
    FAetherStoredAggregate StorageRow;StorageRow.Key={EAetherAggregateKind::Container,Storage.ContainerId};
    if(!TestTrue(TEXT("Encode personal storage"),AetherContainerCodec::Encode(Storage,D.Items,StorageRow.Payload,Why)))return false;
    TestTrue(TEXT("Bootstrap wins last snapshot-visible slot"),CreateEmptyContainer(DB,StorageRow,Seed.ActorId).Code==EAetherStoreCode::Found);
    FAetherTransaction Drop;Drop.ActorId=Seed.ActorId;Drop.ExpectedProfileRevision=0;Drop.CommandId=AetherTransactions::NewCommandId(0);Drop.Request={2};
    P.ExpectedRevision=0;P.Value.Key={EAetherAggregateKind::Profile,Seed.ActorId};P.Value.Revision=1;Drop.Writes.Add(P);
    FAetherAggregateWrite New;New.Value.Key={EAetherAggregateKind::Container,TEXT("Drop.New")};New.Value.Payload={1};Drop.Writes.Add(New);
    TestTrue(TEXT("Stale drop admission is rejected in write transaction"),CommitTransaction(DB,Drop,Options).Code==EAetherStoreCode::Capacity);
    TestEqual(TEXT("Rejected batch leaves profile untouched"),ReadAggregate(DB,{EAetherAggregateKind::Profile,Seed.ActorId}).Value->Revision,int64(0));
    TestTrue(TEXT("Rejected batch creates no drop"),ReadAggregate(DB,New.Value.Key).Code==EAetherStoreCode::Missing);
    TestTrue(TEXT("Rejected batch writes no success receipt"),LookupReceipt(DB,{Drop.ActorId,Drop.CommandId,Drop.ProtocolVersion,Drop.Request}).Code==EAetherStoreCode::Missing);
    if(!TestTrue(TEXT("Construct full historical registry"),Fill(AetherContainerLimits::DropAdmission-1,AetherContainerLimits::Registry-1)))return false;
    TestEqual(TEXT("Registry contains exactly 4096 rows"),ReadSnapshot(DB,Query).ContainerCount,AetherContainerLimits::Registry);
    Storage.ContainerId=TEXT("Storage_Another");StorageRow.Key.Id=Storage.ContainerId;
    if(!TestTrue(TEXT("Encode another valid bootstrap candidate"),AetherContainerCodec::Encode(Storage,D.Items,StorageRow.Payload,Why)))return false;
    TestTrue(TEXT("Full personal bootstrap returns capacity, not data conflict"),CreateEmptyContainer(DB,StorageRow,Seed.ActorId).Code==EAetherStoreCode::Capacity);
    TestTrue(TEXT("No transaction can create row 4097"),CommitTransaction(DB,Drop,Options).Code==EAetherStoreCode::Capacity);
    TestEqual(TEXT("Full registry remains within audit bounds"),ReadSnapshot(DB,Query).ContainerCount,AetherContainerLimits::Registry);
    FAetherDropSlots Slots;
    Slots.Observe({TEXT("Drop.Legacy0"),{}, {},EAetherContainerKind::WorldDrop,FVector::ZeroVector,false,4});
    const FString Reused=Slots.Resolve(FGuid::NewGuid());
    TestEqual(TEXT("Restored production resolver uses legacy tombstone"),Reused,FString(TEXT("Drop.Legacy0")));
    Slots.Observe({Reused,{}, {},EAetherContainerKind::WorldDrop,FVector::ZeroVector,true,5});
    Slots.Observe({Reused,{}, {},EAetherContainerKind::WorldDrop,FVector::ZeroVector,false,4});
    TestTrue(TEXT("Late publication cannot recycle a newer active slot"),Slots.Resolve(FGuid::NewGuid())!=Reused);
    TestFalse(TEXT("Conflicting same-revision state cannot recycle active slot"),Slots.Observe({Reused,{}, {},EAetherContainerKind::WorldDrop,FVector::ZeroVector,false,5}));
    TestTrue(TEXT("Identical same-revision publication is idempotent"),Slots.Observe({Reused,{}, {},EAetherContainerKind::WorldDrop,FVector::ZeroVector,true,5}));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherContainerCapacitySceneTest,"Aether.Systems.Runtime.ContainerCapacityStaysLocal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherContainerCapacitySceneTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);if(!W)return false;
    ON_SCOPE_EXIT {W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);};
    auto* Mode=W->SpawnActor<AAetherFrontierMode>();if(!Mode)return false;
    Mode->bNativeSceneReady=true;Mode->bNativeBaselineReady=true;
    // Keep independent creation jobs in flight so this minimal fixture never
    // needs a GameInstance/backend just to exercise the production result drain.
    TArray<TUniquePtr<TPromise<FAetherStoreReadResult>>> Waiting;
    for(int32 I=0;I<16;++I)
    {
        auto Promise=MakeUnique<TPromise<FAetherStoreReadResult>>();AAetherFrontierMode::FContainerCreation Job;
        Job.Future=Promise->GetFuture();Mode->NativeContainerCreates.Add(FString::Printf(TEXT("Pending%d"),I),MoveTemp(Job));Waiting.Add(MoveTemp(Promise));
    }
    TPromise<FAetherStoreReadResult> Full;AAetherFrontierMode::FContainerCreation Job;
    Job.Expected.ContainerId=TEXT("Storage_NewPlayer");Job.Expected.Kind=EAetherContainerKind::PersonalStorage;Job.Expected.OwnerCharacterId=TEXT("NewPlayer");
    Job.Future=Full.GetFuture();const FString JobId=Job.Expected.ContainerId;Mode->NativeContainerCreates.Add(JobId,MoveTemp(Job));
    FAetherStoreReadResult Capacity;Capacity.Code=EAetherStoreCode::Capacity;Full.SetValue(MoveTemp(Capacity));
    Mode->TickNativeContainers();
    TestTrue(TEXT("Full-registry new-player stash does not fail whole scene"),Mode->NativeSceneReady()&&!Mode->bWorldRestoreFailed&&!Mode->bNativeFailureReported);
    TestTrue(TEXT("Unavailable stash receives local retry"),Mode->NativeContainerRetry.Contains(TEXT("Storage_NewPlayer")));
    TestFalse(TEXT("Completed rejected request leaves pending jobs"),Mode->NativeContainerCreates.Contains(TEXT("Storage_NewPlayer")));
    Mode->NativeContainerCreates.Reset();
    for(auto& Promise:Waiting){FAetherStoreReadResult R;R.Code=EAetherStoreCode::Busy;Promise->SetValue(MoveTemp(R));}
    return true;
}
#endif
