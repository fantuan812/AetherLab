#include "Misc/AutomationTest.h"
#include "Persistence/AetherSqliteStore.h"
#include "Misc/Paths.h"
#include "../Persistence/AetherSqliteInternal.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FAetherLegacyImport Fixture()
{
    FAetherLegacyImport I;I.SourceSha256=FString::ChrN(64,'a');
    for(int32 N=0;N<3;++N)
    {
        FAetherStoredAggregate V;V.Key={N==2?EAetherAggregateKind::World:EAetherAggregateKind::Profile,N==0?TEXT("Alice"):N==1?TEXT("Bob"):TEXT("World")};
        V.Revision=N==0?7:N==1?19:33;V.Payload={uint8(N+1)};I.Values.Add(V);
    }
    return I;
}
FAetherSqliteOptions Options()
{
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10Import")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    O.Fault=MakeShared<TAtomic<EAetherStoreFault>,ESPMode::ThreadSafe>(EAetherStoreFault::None);return O;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherImportContractTest,"Aether.V10.Store.ImportBoundsAndReservedMarker",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherImportContractTest::RunTest(const FString&)
{
    auto I=Fixture();FString Reason;
    TestTrue(TEXT("Valid complete snapshot"),AetherImports::Validate(I,Reason));
    auto Bad=I;Bad.SourceSha256=TEXT("not-a-hash");TestFalse(TEXT("Source must be explicit SHA256"),AetherImports::Validate(Bad,Reason));
    Bad=I;Bad.SourceSchema=99;TestFalse(TEXT("Unknown old schema rejected"),AetherImports::Validate(Bad,Reason));
    Bad=I;Bad.Values.RemoveAt(2);TestFalse(TEXT("Profiles cannot import without world"),AetherImports::Validate(Bad,Reason));
    Bad=I;const auto Copy=Bad.Values[0];Bad.Values.Add(Copy);TestFalse(TEXT("Duplicate aggregate rejected"),AetherImports::Validate(Bad,Reason));
    Bad=I;Bad.Values[0].Key.Kind=EAetherAggregateKind::Migration;TestFalse(TEXT("Caller cannot inject its own marker"),AetherImports::Validate(Bad,Reason));
    Bad=I;Bad.Values[0].Payload.SetNum(AetherTransactions::MaxPayloadBytes+1);TestFalse(TEXT("Oversized row rejected before queue"),AetherImports::Validate(Bad,Reason));
    Bad=I;Bad.Values[0].Revision=-1;TestFalse(TEXT("Unknown legacy version cannot become a new row"),AetherImports::Validate(Bad,Reason));
    FAetherTransaction T;T.ActorId=TEXT("Alice");T.CommandId=AetherTransactions::NewCommandId(-1);T.Request={1};
    FAetherAggregateWrite W;W.Value=I.Values[0];W.Value.Revision=0;T.Writes.Add(W);
    W.Value.Key={EAetherAggregateKind::Migration,TEXT("LegacyV9")};T.Writes.Add(W);
    TestFalse(TEXT("Ordinary commands cannot rewrite import provenance"),AetherTransactions::Validate(T,Reason));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherImportRecoveryTest,"Aether.V10.Store.AtomicLegacyImportReplayAndNoOverwrite",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherImportRecoveryTest::RunTest(const FString&)
{
    const auto Import=Fixture();const auto O=Options();auto Opened=AetherSQLite::Open(O);
    if(!TestTrue(TEXT("Open fresh migration destination"),Opened.Store.IsValid()))return false;
    O.Fault->Store(EAetherStoreFault::AfterFirstWrite);
    TestTrue(TEXT("Partial import fails"),Opened.Store->ImportLegacy(Import).Get().Code==EAetherStoreCode::Unavailable);
    for(const auto& V:Import.Values)TestTrue(TEXT("No partial row escapes rollback"),Opened.Store->Read(V.Key).Get().Code==EAetherStoreCode::Missing);
    const FAetherAggregateKey Marker{EAetherAggregateKind::Migration,TEXT("LegacyV9")};
    TestTrue(TEXT("No false completed marker"),Opened.Store->Read(Marker).Get().Code==EAetherStoreCode::Missing);
    O.Fault->Store(EAetherStoreFault::AfterCommitBeforeReply);
    TestTrue(TEXT("Simulate lost committed response"),Opened.Store->ImportLegacy(Import).Get().Code==EAetherStoreCode::Unavailable);
    Opened.Store->Close();Opened.Store.Reset();Opened=AetherSQLite::Open(O);
    if(!TestTrue(TEXT("Reopen imported database"),Opened.Store.IsValid()))return false;
    for(const auto& V:Import.Values)
    {
        const auto Row=Opened.Store->Read(V.Key).Get();
        TestTrue(TEXT("Every original version and payload retained"),Row.Value.IsSet()&&Row.Value->Revision==V.Revision&&Row.Value->Payload==V.Payload);
    }
    auto Reordered=Import;Reordered.Values.Swap(0,2);
    TestTrue(TEXT("Reordered same snapshot replays"),Opened.Store->ImportLegacy(Reordered).Get().Code==EAetherStoreCode::Replayed);
    auto Changed=Import;Changed.Values[0].Payload={99};
    TestTrue(TEXT("Same source cannot substitute converted content"),Opened.Store->ImportLegacy(Changed).Get().Code==EAetherStoreCode::Conflict);
    Changed=Import;Changed.SourceSha256=FString::ChrN(64,'b');
    TestTrue(TEXT("A second old save cannot overwrite imported state"),Opened.Store->ImportLegacy(Changed).Get().Code==EAetherStoreCode::Conflict);
    FAetherTransaction Advance;Advance.ActorId=TEXT("Alice");Advance.ExpectedProfileRevision=7;
    Advance.CommandId=AetherTransactions::NewCommandId(7);Advance.Request={42};
    FAetherAggregateWrite W;W.Value=Import.Values[0];W.ExpectedRevision=7;W.Value.Revision=8;W.Value.Payload={42};Advance.Writes.Add(W);
    TestTrue(TEXT("Normal command continues from preserved old revision"),Opened.Store->Commit(Advance).Get().Code==EAetherStoreCode::Committed);
    TestTrue(TEXT("Reimport after gameplay is still a no-op"),Opened.Store->ImportLegacy(Import).Get().Code==EAetherStoreCode::Replayed);
    const auto Row=Opened.Store->Read(Import.Values[0].Key).Get();
    TestTrue(TEXT("Reimport never restores outdated player state"),Row.Value.IsSet()&&Row.Value->Revision==8&&Row.Value->Payload==TArray<uint8>{42});
    Opened.Store->Close();Opened.Store.Reset();
    // 仅损坏这个合成数据库：保留标识却删掉一个角色，验证重试不会伪报完整导入。
    sqlite3* DB=nullptr;FTCHARToUTF8 Path(*O.DatabasePath);
    if(TestEqual(TEXT("Open synthetic logical corruption fixture"),sqlite3_open(Path.Get(),&DB),SQLITE_OK))
    {
        TestTrue(TEXT("Remove one fixture row"),AetherSQLite::Private::Exec(DB,"DELETE FROM aggregates WHERE kind=0 AND id='Bob'"));
        sqlite3_close(DB);
    }
    Opened=AetherSQLite::Open(O);
    if(!TestTrue(TEXT("Database remains physically readable"),Opened.Store.IsValid()))return false;
    TestTrue(TEXT("Marker without imported row is corrupt, not replayed"),Opened.Store->ImportLegacy(Import).Get().Code==EAetherStoreCode::Corrupt);
    Opened.Store->Close();
    // 没有迁移标识但已有正常数据的数据库同样不能被导入覆盖。
    Opened=AetherSQLite::Open(Options());Advance.ExpectedProfileRevision=-1;Advance.CommandId=AetherTransactions::NewCommandId(-1);
    Advance.Writes[0].ExpectedRevision=-1;Advance.Writes[0].Value.Revision=0;
    TestTrue(TEXT("Seed separate live destination"),Opened.Store.IsValid()&&Opened.Store->Commit(Advance).Get().Code==EAetherStoreCode::Committed);
    if(Opened.Store){TestTrue(TEXT("Nonempty live destination rejects migration"),Opened.Store->ImportLegacy(Import).Get().Code==EAetherStoreCode::Conflict);Opened.Store->Close();}
    return true;
}
#endif
