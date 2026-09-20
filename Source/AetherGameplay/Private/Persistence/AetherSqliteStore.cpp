#include "Persistence/AetherSqliteStore.h"
#include "AetherSqliteInternal.h"
#include "Contracts/AetherPlayerCommand.h"
#include "World/AetherContainerCodec.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTLS.h"
#include "Misc/Paths.h"

namespace AetherSQLite::Private
{
template<typename Result>
TFuture<Result> Completed(Result Value)
{
    TPromise<Result> Promise;
    auto Future=Promise.GetFuture();
    Promise.SetValue(MoveTemp(Value));
    return Future;
}

class FStore final : public IAetherTransactionalStore, public FRunnable
{
    using FJob = TUniqueFunction<void(sqlite3*)>;
    FAetherSqliteOptions Options;
    TQueue<FJob, EQueueMode::Mpsc> Jobs;
    FCriticalSection Mutex;
    FCriticalSection ShutdownMutex;
    int32 Pending = 0;
    bool ImportPending = false;
    bool Accepting = true;
    FEvent* Wake = FPlatformProcess::GetSynchEventFromPool(false);
    TUniquePtr<FRunnableThread> Thread;
    TPromise<FAetherStoreResult> Ready;

    template<typename Result, typename Function>
    TFuture<Result> Enqueue(Function Work, Result Rejected, bool IsImport=false)
    {
        auto Promise = MakeShared<TPromise<Result>, ESPMode::ThreadSafe>();
        auto Future = Promise->GetFuture();
        {
            FScopeLock Lock(&Mutex);
            if (!Accepting || Pending >= Options.QueueCapacity || (IsImport && ImportPending))
            {
                Promise->SetValue(MoveTemp(Rejected));
                return Future;
            }
            ++Pending;
            ImportPending |= IsImport;
            Jobs.Enqueue([this, Promise, IsImport, Work=MoveTemp(Work)](sqlite3* DB) mutable {
                auto Value=Work(DB);
                if(IsImport){FScopeLock ImportLock(&Mutex);ImportPending=false;}
                Promise->SetValue(MoveTemp(Value));
            });
        }
        Wake->Trigger();
        return Future;
    }

    FAetherStoreResult Initialize(sqlite3*& DB)
    {
        FAetherStoreResult R;
        if (sqlite3_libversion_number()!=3053004) { R.Detail=TEXT("Pinned SQLite runtime version mismatch"); return R; }
        const FString Directory = FPaths::GetPath(Options.DatabasePath);
        if (!IFileManager::Get().MakeDirectory(*Directory, true)) { R.Detail=TEXT("Cannot create database directory"); return R; }
        FTCHARToUTF8 Path(*Options.DatabasePath);
        const int OpenCode = sqlite3_open_v2(Path.Get(), &DB, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
        if (OpenCode != SQLITE_OK) { R.Detail=Error(DB); return R; }
        sqlite3_extended_result_codes(DB, 1);
        sqlite3_busy_timeout(DB, 250);
        int64 Version = -1, App = -1, Tables = -1;
        {
            FStatement Q(DB, "PRAGMA user_version"); if (Q.Step()==SQLITE_ROW) Version=Q.ColumnInt(0);
            FStatement A(DB, "PRAGMA application_id"); if (A.Step()==SQLITE_ROW) App=A.ColumnInt(0);
            FStatement T(DB, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"); if (T.Step()==SQLITE_ROW) Tables=T.ColumnInt(0);
        }
        // 未知/损坏数据库原样保留，绝不清表或按空档覆盖。
        if (Version < 0 || App < 0 || Tables < 0) { R.Code=EAetherStoreCode::Corrupt; R.Detail=Error(DB); return R; }
        const bool Fresh = Version==0 && App==0 && Tables==0;
        if (!Fresh && (Version!=Schema || App!=ApplicationId))
        { R.Code=EAetherStoreCode::UnsupportedSchema; R.Detail=TEXT("Unknown database schema/application; original preserved"); return R; }
        if (!Exec(DB, "PRAGMA journal_mode=WAL") || !Exec(DB, "PRAGMA synchronous=FULL") || !Exec(DB, "PRAGMA foreign_keys=ON"))
        { R.Detail=Error(DB); return R; }
        {
            FStatement Journal(DB, "PRAGMA journal_mode");
            FStatement Sync(DB, "PRAGMA synchronous");
            if (Journal.Step()!=SQLITE_ROW || Journal.ColumnText(0)!=TEXT("wal") || Sync.Step()!=SQLITE_ROW || Sync.ColumnInt(0)!=2)
            { R.Detail=TEXT("Required WAL/FULL durability settings unavailable"); return R; }
        }
        if (Fresh)
        {
            FTransactionGuard Tx(DB);
            if (!Tx.Active || !Exec(DB,
                "CREATE TABLE aggregates(kind INTEGER NOT NULL,id TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>=0),schema INTEGER NOT NULL,payload BLOB NOT NULL,PRIMARY KEY(kind,id));"
                "CREATE TABLE receipts(actor TEXT NOT NULL,command TEXT NOT NULL,protocol INTEGER NOT NULL,request BLOB NOT NULL,result BLOB NOT NULL,final_revision INTEGER NOT NULL,PRIMARY KEY(actor,command));"
                "CREATE INDEX receipt_window ON receipts(actor,final_revision DESC);"
                "CREATE TABLE effects(actor TEXT NOT NULL,id TEXT NOT NULL,schema INTEGER NOT NULL,payload BLOB NOT NULL,PRIMARY KEY(actor,id));"
                "PRAGMA application_id=1095053616;PRAGMA user_version=10;") || !Tx.Commit())
            { R.Detail=Error(DB); return R; }
        }
        FStatement Integrity(DB, "PRAGMA quick_check");
        if (Integrity.Step()!=SQLITE_ROW || Integrity.ColumnText(0)!=TEXT("ok"))
        { R.Code=EAetherStoreCode::Corrupt; R.Detail=TEXT("Database integrity check failed"); return R; }
        FStatement TablesCheck(DB, "SELECT (SELECT count(*) FROM aggregates),(SELECT count(*) FROM receipts),(SELECT count(*) FROM effects)");
        if (TablesCheck.Step()!=SQLITE_ROW) { R.Code=EAetherStoreCode::Corrupt; R.Detail=Error(DB); return R; }
        R.Code=EAetherStoreCode::Ready;
        return R;
    }

public:
    explicit FStore(FAetherSqliteOptions In) : Options(MoveTemp(In)) {}
    virtual ~FStore() override { Close(); FPlatformProcess::ReturnSynchEventToPool(Wake); }
    FAetherStoreResult Start()
    {
        auto Future=Ready.GetFuture();
        Thread.Reset(FRunnableThread::Create(this, TEXT("AetherSQLiteWriter")));
        if (!Thread) { FAetherStoreResult ErrorResult; ErrorResult.Detail=TEXT("Cannot create store worker"); Ready.SetValue(MoveTemp(ErrorResult)); }
        return Future.Get();
    }
    virtual uint32 Run() override
    {
        sqlite3* DB=nullptr;
        auto Result=Initialize(DB);
        const bool OK=Result.Code==EAetherStoreCode::Ready;
        Ready.SetValue(MoveTemp(Result));
        if (OK) for (;;)
        {
            FJob Job;
            if (Jobs.Dequeue(Job))
            {
                Job(DB);
                FScopeLock Lock(&Mutex); --Pending;
                continue;
            }
            { FScopeLock Lock(&Mutex); if (!Accepting) break; }
            Wake->Wait();
        }
        // 必须由连接所有者线程关闭；关闭前队列已排空，不能让后台写访问析构的状态。
        if (DB) sqlite3_close_v2(DB);
        return 0;
    }
    virtual void Close() override
    {
        FScopeLock ShutdownLock(&ShutdownMutex);
        // 协调者在游戏线程结束生命周期，不能从 writer 的 Future 续延里自等待。
        check(!Thread || Thread->GetThreadID()!=FPlatformTLS::GetCurrentThreadId());
        { FScopeLock Lock(&Mutex); Accepting=false; }
        Wake->Trigger();
        if (Thread) { Thread->WaitForCompletion(); Thread.Reset(); }
    }
    virtual TFuture<FAetherStoreResult> Commit(FAetherTransaction T) override
    {
        // 进入队列前就限制载荷，不能先积压超大请求再等待写线程拒绝。
        FString Reason;
        if (!AetherTransactions::Validate(T,Reason))
        {
            FAetherStoreResult Invalid; Invalid.Code=EAetherStoreCode::Invalid; Invalid.Detail=MoveTemp(Reason);
            return Completed(MoveTemp(Invalid));
        }
        FAetherStoreResult Rejected; Rejected.Code=EAetherStoreCode::Busy; Rejected.Detail=TEXT("Writer closed or queue full");
        return Enqueue<FAetherStoreResult>([T=MoveTemp(T), O=Options](sqlite3* DB) { return CommitTransaction(DB,T,O); }, MoveTemp(Rejected));
    }
    virtual TFuture<FAetherStoreResult> LookupReceipt(FAetherReceiptQuery Query) override
    {
        FTCHARToUTF8 Actor(*Query.ActorId);FUTF8ToTCHAR Back(Actor.Get(),Actor.Length());
        bool Valid=!Query.ActorId.IsEmpty()&&Query.ActorId.Len()<=128&&FString(Back.Length(),Back.Get())==Query.ActorId;
        for(TCHAR C:Query.ActorId)Valid&=C>=32;
        if(!Valid||!Query.CommandId.IsValid()||!AetherCommands::IsSupportedProtocol(Query.ProtocolVersion)||Query.Request.IsEmpty()||Query.Request.Num()>16384)
        {FAetherStoreResult R;R.Code=EAetherStoreCode::Invalid;return Completed(MoveTemp(R));}
        FAetherStoreResult Rejected;Rejected.Code=EAetherStoreCode::Busy;
        return Enqueue<FAetherStoreResult>([Query=MoveTemp(Query)](sqlite3* DB){return AetherSQLite::Private::LookupReceipt(DB,Query);},MoveTemp(Rejected));
    }
    virtual TFuture<FAetherStoreResult> ImportLegacy(FAetherLegacyImport Import) override
    {
        FString Reason;
        if(!AetherImports::Validate(Import,Reason))
        {FAetherStoreResult Invalid;Invalid.Code=EAetherStoreCode::Invalid;Invalid.Detail=MoveTemp(Reason);return Completed(MoveTemp(Invalid));}
        // 同时最多一份大导入负载入队，避免 64 个 32 MiB 快照挤占游戏内存。
        FAetherStoreResult Rejected;Rejected.Code=EAetherStoreCode::Busy;Rejected.Detail=TEXT("Import already queued or writer unavailable");
        return Enqueue<FAetherStoreResult>([Import=MoveTemp(Import),O=Options](sqlite3* DB){return AetherSQLite::Private::ImportLegacy(DB,Import,O);},MoveTemp(Rejected),true);
    }
    virtual TFuture<FAetherStoreResult> InitializeWorld(FAetherStoredAggregate World) override
    {
        if(!ValidInitialAggregate(World,EAetherAggregateKind::World))
        {FAetherStoreResult R;R.Code=EAetherStoreCode::Invalid;return Completed(MoveTemp(R));}
        FAetherStoreResult Rejected;Rejected.Code=EAetherStoreCode::Busy;
        return Enqueue<FAetherStoreResult>([World=MoveTemp(World)](sqlite3* DB){return AetherSQLite::Private::InitializeWorld(DB,World);},MoveTemp(Rejected),true);
    }
    virtual TFuture<FAetherStoreReadResult> CreateProfile(FAetherStoredAggregate Profile) override
    {
        if(!ValidInitialAggregate(Profile,EAetherAggregateKind::Profile))
        {FAetherStoreReadResult R;R.Code=EAetherStoreCode::Invalid;return Completed(MoveTemp(R));}
        FAetherStoreReadResult Rejected;Rejected.Code=EAetherStoreCode::Busy;
        return Enqueue<FAetherStoreReadResult>([Profile=MoveTemp(Profile)](sqlite3* DB){return AetherSQLite::Private::CreateProfile(DB,Profile);},MoveTemp(Rejected));
    }
    virtual TFuture<FAetherStoreReadResult> CreateEmptyContainer(FAetherContainerStateV10 Container,FAetherV10ItemDefinitions Definitions) override
    {
        FAetherStoredAggregate Row;Row.Key={EAetherAggregateKind::Container,Container.ContainerId};Row.Revision=0;FString Why;
        if(Container.Revision!=0||!Container.bActive||Container.Kind==EAetherContainerKind::WorldDrop||!Container.Inventory.Items.IsEmpty()||
            !AetherContainerCodec::Encode(Container,Definitions,Row.Payload,Why))
        {FAetherStoreReadResult R;R.Code=EAetherStoreCode::Invalid;R.Detail=Why;return Completed(MoveTemp(R));}
        FAetherStoreReadResult Rejected;Rejected.Code=EAetherStoreCode::Busy;
        return Enqueue<FAetherStoreReadResult>([Row=MoveTemp(Row),Owner=Container.OwnerCharacterId](sqlite3* DB){return AetherSQLite::Private::CreateEmptyContainer(DB,Row,Owner);},MoveTemp(Rejected));
    }
    virtual TFuture<FAetherStoreReadResult> CompareExchangeWorld(FAetherAggregateWrite Write) override
    {
        if(!ValidWorldCheckpoint(Write))
        {FAetherStoreReadResult R;R.Code=EAetherStoreCode::Invalid;return Completed(MoveTemp(R));}
        FAetherStoreReadResult Rejected;Rejected.Code=EAetherStoreCode::Busy;
        return Enqueue<FAetherStoreReadResult>([Write=MoveTemp(Write)](sqlite3* DB){return AetherSQLite::Private::CompareExchangeWorld(DB,Write);},MoveTemp(Rejected));
    }
    virtual TFuture<FAetherStoreReadResult> Read(FAetherAggregateKey Key) override
    { return Enqueue<FAetherStoreReadResult>([Key=MoveTemp(Key)](sqlite3* DB){ return ReadAggregate(DB,Key); }, FAetherStoreReadResult()); }
    virtual TFuture<FAetherStoreRevisionIndex> ReadRevisions(EAetherAggregateKind Kind) override
    {
        if(uint8(Kind)>uint8(EAetherAggregateKind::Container))
        {FAetherStoreRevisionIndex R;R.Code=EAetherStoreCode::Invalid;return Completed(MoveTemp(R));}
        return Enqueue<FAetherStoreRevisionIndex>([Kind](sqlite3* DB){return AetherSQLite::Private::ReadRevisions(DB,Kind);},FAetherStoreRevisionIndex());
    }
    virtual TFuture<FAetherStoreSnapshotResult> ReadSnapshot(FAetherStoreSnapshotQuery Query) override
    {
        bool Valid=!Query.Keys.IsEmpty()&&Query.Keys.Num()<=4;TSet<FAetherAggregateKey> Seen;
        for(const auto& K:Query.Keys)
        {
            FTCHARToUTF8 U(*K.Id);FUTF8ToTCHAR Back(U.Get(),U.Length());
            Valid&=uint8(K.Kind)<=uint8(EAetherAggregateKind::Container)&&!K.Id.IsEmpty()&&K.Id.Len()<=128&&
                FString(Back.Length(),Back.Get())==K.Id&&!Seen.Contains(K);
            for(TCHAR C:K.Id)Valid&=C>=32;Seen.Add(K);
        }
        if(!Valid){FAetherStoreSnapshotResult R;R.Code=EAetherStoreCode::Invalid;return Completed(MoveTemp(R));}
        return Enqueue<FAetherStoreSnapshotResult>([Query=MoveTemp(Query)](sqlite3* DB){return AetherSQLite::Private::ReadSnapshot(DB,Query);},FAetherStoreSnapshotResult());
    }
    virtual TFuture<FAetherStoreEffectsResult> PendingEffects(FString Actor) override
    { return Enqueue<FAetherStoreEffectsResult>([Actor=MoveTemp(Actor)](sqlite3* DB){ return ReadEffects(DB,Actor); }, FAetherStoreEffectsResult()); }
    virtual TFuture<bool> AcknowledgeEffect(FString Actor, FGuid Id) override
    {
        return Enqueue<bool>([Actor=MoveTemp(Actor), Id](sqlite3* DB) {
            FStatement Q(DB,"DELETE FROM effects WHERE actor=? AND id=?");
            return Q.Text(1,Actor) && Q.Text(2,Id.ToString(EGuidFormats::Digits)) && Q.Step()==SQLITE_DONE;
        }, false);
    }
    virtual TFuture<bool> Backup(FString Destination) override
    { return Enqueue<bool>([Destination=MoveTemp(Destination)](sqlite3* DB){ return MakeBackup(DB,Destination); }, false); }
};
}

FAetherSqliteOpenResult AetherSQLite::Open(FAetherSqliteOptions Options)
{
    FAetherSqliteOpenResult Result;
    if (Options.DatabasePath.IsEmpty() || Options.QueueCapacity<1 || Options.QueueCapacity>256)
    { Result.Code=EAetherStoreCode::Invalid; Result.Detail=TEXT("Invalid store path or queue bound"); return Result; }
    Options.DatabasePath=FPaths::ConvertRelativePathToFull(Options.DatabasePath);
    auto Store=MakeShared<Private::FStore,ESPMode::ThreadSafe>(MoveTemp(Options));
    auto Ready=Store->Start();
    Result.Code=Ready.Code; Result.Detail=MoveTemp(Ready.Detail);
    if (Ready.Code==EAetherStoreCode::Ready) Result.Store=Store;
    return Result;
}
FString AetherSQLite::RuntimeVersion() { return UTF8_TO_TCHAR(sqlite3_libversion()); }
