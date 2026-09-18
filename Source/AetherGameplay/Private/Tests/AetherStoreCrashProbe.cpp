#include "Misc/AutomationTest.h"
#include "Persistence/AetherSqliteStore.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

// 单独命名，不进入普通 Aether.V10 规则筛选。只由隔离子进程脚本执行。
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherStoreCrashProbe,"Aether.CrashProbe.SQLiteProcessRecovery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherStoreCrashProbe::RunTest(const FString&)
{
    FString Path,Phase;
    if (!FParse::Value(FCommandLine::Get(),TEXT("AetherStoreProbeDb="),Path)
        || !FParse::Value(FCommandLine::Get(),TEXT("AetherStoreProbePhase="),Phase)
        || !FParse::Param(FCommandLine::Get(),TEXT("AetherAllowSyntheticStoreCrash")))
    { AddError(TEXT("Crash probe requires explicit isolated-process arguments")); return false; }
    Path=FPaths::ConvertRelativePathToFull(Path);
    const FString Allowed=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/V10Crash"));
    if (!FPaths::IsUnderDirectory(Path,Allowed))
    { AddError(TEXT("Crash probe refuses paths outside its synthetic fixture directory")); return false; }

    FAetherSqliteOptions Options; Options.DatabasePath=Path;
    Options.Fault=MakeShared<TAtomic<EAetherStoreFault>,ESPMode::ThreadSafe>(EAetherStoreFault::None);
    auto Opened=AetherSQLite::Open(Options);
    if (!TestTrue(TEXT("Open crash probe database"),Opened.Store.IsValid())) { AddError(Opened.Detail); return false; }
    auto Store=Opened.Store;
    const auto Make=[](int64 Expected,uint8 Value) {
        FAetherTransaction T; T.ActorId=TEXT("CrashFixture"); T.ExpectedProfileRevision=Expected;
        // 固定身份用于跨进程重试；高 64 位仍绑定原请求版本。
        T.CommandId=FGuid(0,uint32(Expected+1),0x43524153,1);
        T.Request={Value}; T.Result={Value};
        for (auto Kind:{EAetherAggregateKind::Profile,EAetherAggregateKind::World})
        {
            FAetherAggregateWrite W; W.ExpectedRevision=Expected;
            W.Value.Key={Kind,TEXT("CrashFixture")}; W.Value.Revision=Expected+1; W.Value.Payload={Value};
            T.Writes.Add(W);
        }
        return T;
    };
    auto Command=Make(0,2);
    Command.Effects.Add({FGuid(0,1,0x43524153,2),Command.ActorId,1,{90,100}});
    if (Phase==TEXT("Seed"))
    {
        TestTrue(TEXT("Initial paired records committed"),Store->Commit(Make(-1,1)).Get().Code==EAetherStoreCode::Committed);
    }
    else if (Phase==TEXT("CrashBefore") || Phase==TEXT("CrashAfter"))
    {
        Options.Fault->Store(Phase==TEXT("CrashBefore")?EAetherStoreFault::CrashBeforeCommit:EAetherStoreFault::CrashAfterCommit);
        Store->Commit(Command).Get();
        AddError(TEXT("Expected process termination did not occur"));
    }
    else if (Phase==TEXT("VerifyBefore") || Phase==TEXT("VerifyAfter"))
    {
        const bool Committed=Phase==TEXT("VerifyAfter");
        for (auto Kind:{EAetherAggregateKind::Profile,EAetherAggregateKind::World})
        {
            auto R=Store->Read({Kind,TEXT("CrashFixture")}).Get();
            TestTrue(TEXT("Both aggregates recover same commit boundary"),R.Code==EAetherStoreCode::Found && R.Value.IsSet()
                && R.Value->Revision==(Committed?1:0) && R.Value->Payload==TArray<uint8>{uint8(Committed?2:1)});
        }
        auto Effects=Store->PendingEffects(Command.ActorId).Get();
        TestTrue(TEXT("Delivery and consumption recover atomically"),Effects.Code==EAetherStoreCode::Found && Effects.Values.Num()==(Committed?1:0));
        if (Committed) TestTrue(TEXT("Retry after killed process replays receipt"),Store->Commit(Command).Get().Code==EAetherStoreCode::Replayed);
    }
    else AddError(TEXT("Unknown probe phase"));
    Store->Close();
    return true;
}
#endif
