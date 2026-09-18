#include "AetherSqliteInternal.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

namespace AetherSQLite::Private
{
FAetherStoreReadResult ReadAggregate(sqlite3* DB, const FAetherAggregateKey& Key)
{
    FAetherStoreReadResult R;
    FStatement Q(DB,"SELECT revision,schema,payload FROM aggregates WHERE kind=? AND id=?");
    if (!Q.Int(1,uint8(Key.Kind)) || !Q.Text(2,Key.Id)) { R.Detail=Error(DB); return R; }
    const int Step=Q.Step();
    if (Step==SQLITE_DONE) { R.Code=EAetherStoreCode::Missing; return R; }
    if (Step!=SQLITE_ROW) { R.Detail=Error(DB); return R; }
    FAetherStoredAggregate V;
    V.Key=Key; V.Revision=Q.ColumnInt(0); V.SchemaVersion=int32(Q.ColumnInt(1));
    if (V.SchemaVersion!=Schema) { R.Code=EAetherStoreCode::UnsupportedSchema; return R; }
    if (V.Revision<0 || !Q.ColumnBlob(2,V.Payload) || V.Payload.IsEmpty())
    { R.Code=EAetherStoreCode::Corrupt; R.Detail=TEXT("Invalid stored aggregate"); return R; }
    R.Code=EAetherStoreCode::Found; R.Value=MoveTemp(V);
    return R;
}

FAetherStoreResult LookupReceipt(sqlite3* DB,const FAetherReceiptQuery& Q)
{
    FAetherStoreResult R;
    FStatement Receipt(DB,"SELECT protocol,request,result,final_revision FROM receipts WHERE actor=? AND command=?");
    if(!Receipt.Text(1,Q.ActorId)||!Receipt.Text(2,Q.CommandId.ToString(EGuidFormats::Digits))){R.Detail=Error(DB);return R;}
    const int Step=Receipt.Step();
    if(Step==SQLITE_DONE){R.Code=EAetherStoreCode::Missing;return R;}
    if(Step!=SQLITE_ROW){R.Detail=Error(DB);return R;}
    TArray<uint8> SavedRequest;
    if(!Receipt.ColumnBlob(1,SavedRequest,16384)||!Receipt.ColumnBlob(2,R.Result,16384)||Receipt.ColumnInt(3)<0)
    {R.Code=EAetherStoreCode::Corrupt;R.Detail=TEXT("Invalid persisted receipt");return R;}
    if(Receipt.ColumnInt(0)!=Q.ProtocolVersion||SavedRequest!=Q.Request)
    {R.Code=EAetherStoreCode::Conflict;R.Result.Reset();R.Detail=TEXT("Command ID reused with different payload");return R;}
    R.Code=EAetherStoreCode::Replayed;R.FinalProfileRevision=Receipt.ColumnInt(3);return R;
}

FAetherStoreResult CommitTransaction(sqlite3* DB, const FAetherTransaction& T, const FAetherSqliteOptions& Options)
{
    FAetherStoreResult R;
    if (!AetherTransactions::Validate(T,R.Detail)) { R.Code=EAetherStoreCode::Invalid; return R; }
    auto Failure=[&](EAetherStoreCode Code=EAetherStoreCode::Unavailable) {
        R.Code=Code; R.Detail=Error(DB); return R;
    };
    FTransactionGuard Tx(DB);
    if (!Tx.Active) return Failure((sqlite3_errcode(DB)&0xff)==SQLITE_BUSY ? EAetherStoreCode::Busy : EAetherStoreCode::Unavailable);

    // 外层预查询只优化重试；真正提交仍在此写事务内再次检查，消除检查/提交之间的竞争窗口。
    const auto ExistingReceipt=LookupReceipt(DB,{T.ActorId,T.CommandId,T.ProtocolVersion,T.Request});
    if(ExistingReceipt.Code!=EAetherStoreCode::Missing)return ExistingReceipt;

    const auto Profile=ReadAggregate(DB,{EAetherAggregateKind::Profile,T.ActorId});
    if (Profile.Code!=EAetherStoreCode::Found && Profile.Code!=EAetherStoreCode::Missing)
    { R.Code=Profile.Code; R.Detail=Profile.Detail; return R; }
    const int64 Current=Profile.Value.IsSet()?Profile.Value->Revision:-1;
    if (Current!=T.ExpectedProfileRevision)
    {
        R.Code=T.ExpectedProfileRevision<Current?EAetherStoreCode::Expired:EAetherStoreCode::Conflict;
        R.FinalProfileRevision=Current; R.Detail=TEXT("Profile version changed; synchronize before a new command");
        return R;
    }
    R.FinalProfileRevision=Current;
    int64 CommittedProfileRevision=Current;
    // 每个跨域读写版本一起验证；任一目标被其他事务改动，整批候选都不发布。
    for (const auto& W : T.Writes)
    {
        auto Existing=ReadAggregate(DB,W.Value.Key);
        if (Existing.Code!=EAetherStoreCode::Found && Existing.Code!=EAetherStoreCode::Missing)
        { R.Code=Existing.Code; R.Detail=Existing.Detail; return R; }
        const int64 Revision=Existing.Value.IsSet()?Existing.Value->Revision:-1;
        if (Revision!=W.ExpectedRevision) { R.Code=EAetherStoreCode::Conflict; R.Detail=TEXT("Aggregate version changed"); return R; }
    }
    if (!T.Effects.IsEmpty())
    {
        FStatement Count(DB,"SELECT count(*) FROM effects WHERE actor=?");
        if (!Count.Text(1,T.ActorId) || Count.Step()!=SQLITE_ROW) return Failure();
        if (Count.ColumnInt(0)+T.Effects.Num()>128)
        { R.Code=EAetherStoreCode::Busy; R.Detail=TEXT("Pending effect delivery bound reached"); return R; }
    }
#if WITH_DEV_AUTOMATION_TESTS
    const auto Fault=Options.Fault ? Options.Fault->Exchange(EAetherStoreFault::None) : EAetherStoreFault::None;
#endif
    int32 Writes=0;
    for (const auto& W : T.Writes)
    {
        const auto& V=W.Value;
        FStatement Put(DB,"INSERT INTO aggregates(kind,id,revision,schema,payload) VALUES(?,?,?,?,?) ON CONFLICT(kind,id) DO UPDATE SET revision=excluded.revision,schema=excluded.schema,payload=excluded.payload");
        if (!Put.Int(1,uint8(V.Key.Kind)) || !Put.Text(2,V.Key.Id) || !Put.Int(3,V.Revision) || !Put.Int(4,V.SchemaVersion)
            || !Put.Blob(5,V.Payload) || Put.Step()!=SQLITE_DONE) return Failure();
        if (V.Key.Kind==EAetherAggregateKind::Profile && V.Key.Id.Equals(T.ActorId,ESearchCase::CaseSensitive)) CommittedProfileRevision=V.Revision;
        ++Writes;
#if WITH_DEV_AUTOMATION_TESTS
        if (Writes==1 && Fault==EAetherStoreFault::AfterFirstWrite)
        { R.Code=EAetherStoreCode::Unavailable; R.Detail=TEXT("Injected failure after first write"); return R; }
#endif
    }
    for (const auto& E : T.Effects)
    {
        FStatement Put(DB,"INSERT INTO effects(actor,id,schema,payload) VALUES(?,?,?,?)");
        if (!Put.Text(1,E.ActorId) || !Put.Text(2,E.Id.ToString(EGuidFormats::Digits)) || !Put.Int(3,E.SchemaVersion)
            || !Put.Blob(4,E.Payload) || Put.Step()!=SQLITE_DONE) return Failure();
    }
    {
        FStatement Receipt(DB,"INSERT INTO receipts(actor,command,protocol,request,result,final_revision) VALUES(?,?,?,?,?,?)");
        if (!Receipt.Text(1,T.ActorId) || !Receipt.Text(2,T.CommandId.ToString(EGuidFormats::Digits)) || !Receipt.Int(3,T.ProtocolVersion)
            || !Receipt.Blob(4,T.Request) || !Receipt.Blob(5,T.Result) || !Receipt.Int(6,CommittedProfileRevision) || Receipt.Step()!=SQLITE_DONE) return Failure();
        FStatement Trim(DB,"DELETE FROM receipts WHERE actor=? AND command NOT IN (SELECT command FROM receipts WHERE actor=? ORDER BY final_revision DESC LIMIT 64)");
        if (!Trim.Text(1,T.ActorId) || !Trim.Text(2,T.ActorId) || Trim.Step()!=SQLITE_DONE) return Failure();
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (Fault==EAetherStoreFault::CrashBeforeCommit) FPlatformMisc::RequestExitWithStatus(true,91);
    if (Fault==EAetherStoreFault::BeforeCommit)
    { R.Code=EAetherStoreCode::Unavailable; R.Detail=TEXT("Injected failure before durable commit"); return R; }
#endif
    if (!Tx.Commit()) return Failure();
    R.FinalProfileRevision=CommittedProfileRevision;
#if WITH_DEV_AUTOMATION_TESTS
    if (Fault==EAetherStoreFault::CrashAfterCommit) FPlatformMisc::RequestExitWithStatus(true,92);
    // 模拟数据库已提交但响应丢失：重试必须回放回执，效果记录仍只存在一次。
    if (Fault==EAetherStoreFault::AfterCommitBeforeReply)
    { R.Code=EAetherStoreCode::Unavailable; R.Detail=TEXT("Injected lost response after commit"); return R; }
#endif
    R.Code=EAetherStoreCode::Committed; R.Result=T.Result;
    return R;
}

FAetherStoreEffectsResult ReadEffects(sqlite3* DB, const FString& Actor)
{
    FAetherStoreEffectsResult R;
    FStatement Q(DB,"SELECT id,schema,payload FROM effects WHERE actor=? ORDER BY rowid LIMIT 129");
    if (!Q.Text(1,Actor)) { R.Detail=Error(DB); return R; }
    int Step;
    while ((Step=Q.Step())==SQLITE_ROW)
    {
        FAetherEffectDelivery E; E.ActorId=Actor; E.SchemaVersion=int32(Q.ColumnInt(1));
        if (!FGuid::ParseExact(Q.ColumnText(0),EGuidFormats::Digits,E.Id) || !E.Id.IsValid() || E.SchemaVersion!=1
            || !Q.ColumnBlob(2,E.Payload,16384) || E.Payload.IsEmpty() || R.Values.Num()>=128)
        { R.Code=EAetherStoreCode::Corrupt; R.Detail=TEXT("Invalid pending delivery"); R.Values.Reset(); return R; }
        R.Values.Add(MoveTemp(E));
    }
    R.Code=Step==SQLITE_DONE?EAetherStoreCode::Found:EAetherStoreCode::Unavailable;
    if (Step!=SQLITE_DONE) { R.Values.Reset(); R.Detail=Error(DB); }
    return R;
}

bool MakeBackup(sqlite3* DB, const FString& Path)
{
    // 活跃 WAL 不能直接复制 .db 文件；在线 backup API 生成完整一致的快照。
    if (Path.IsEmpty() || IFileManager::Get().FileExists(*Path)) return false;
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true)) return false;
    sqlite3* Destination=nullptr;
    FTCHARToUTF8 UTF8(*Path);
    if (sqlite3_open_v2(UTF8.Get(),&Destination,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK)
    { if (Destination) sqlite3_close_v2(Destination); return false; }
    sqlite3_backup* Backup=sqlite3_backup_init(Destination,"main",DB,"main");
    bool OK=false;
    if (Backup)
    {
        const double Deadline=FPlatformTime::Seconds()+5;
        int Step;
        do
        {
            Step=sqlite3_backup_step(Backup,256);
            if (Step==SQLITE_BUSY || Step==SQLITE_LOCKED) sqlite3_sleep(10);
        } while ((Step==SQLITE_OK || Step==SQLITE_BUSY || Step==SQLITE_LOCKED) && FPlatformTime::Seconds()<Deadline);
        const int Finish=sqlite3_backup_finish(Backup);
        OK=Step==SQLITE_DONE && Finish==SQLITE_OK;
    }
    sqlite3_close_v2(Destination);
    return OK;
}
}
