#include "AetherSqliteInternal.h"
#include "Definitions/AetherV10Definitions.h"
#include "World/AetherWorldCodec.h"

namespace AetherSQLite::Private
{
bool ValidWorldCheckpoint(const FAetherAggregateWrite& W)
{
    return W.Value.Key.Kind==EAetherAggregateKind::World&&W.Value.Key.Id.Equals(TEXT("Main"),ESearchCase::CaseSensitive)&&
        W.ExpectedRevision>=0&&W.ExpectedRevision<MAX_int64-1&&W.Value.Revision==W.ExpectedRevision+1&&
        W.Value.SchemaVersion==Schema&&!W.Value.Payload.IsEmpty()&&W.Value.Payload.Num()<=AetherTransactions::MaxPayloadBytes;
}
FAetherStoreReadResult CompareExchangeWorld(sqlite3* DB,const FAetherAggregateWrite& W)
{
    FAetherStoreReadResult R;
    if(!ValidWorldCheckpoint(W)){R.Code=EAetherStoreCode::Invalid;return R;}
    FTransactionGuard Tx(DB);
    if(!Tx.Active){R.Code=EAetherStoreCode::Busy;R.Detail=Error(DB);return R;}
    auto Existing=ReadAggregate(DB,W.Value.Key);
    if(Existing.Code!=EAetherStoreCode::Found||!Existing.Value.IsSet())return Existing;
    const auto& Old=Existing.Value.GetValue();
    if(Old.Revision==W.Value.Revision&&Old.SchemaVersion==W.Value.SchemaVersion&&Old.Payload==W.Value.Payload)
    {Existing.Code=EAetherStoreCode::Replayed;return Existing;}
    if(Old.Revision!=W.ExpectedRevision)
    {R.Code=EAetherStoreCode::Conflict;R.Detail=TEXT("World advanced; recapture on the newest snapshot");return R;}

    // 在同一个写事务中检查完整世界和角色引用，不能用读阶段的旧索引替代提交时事实。
    const auto Index=ReadRevisions(DB,EAetherAggregateKind::Profile);
    if(Index.Code!=EAetherStoreCode::Found){R.Code=Index.Code;R.Detail=Index.Detail;return R;}
    const auto& D=FAetherV10Definitions::Get();FAetherWorldStateV10 Before,After;FString Why;
    if(!D.bValid||!AetherWorldCodec::Decode(Old.Payload,D.Items,D.Rules,Index.Revisions,Before,Why)||Before.Revision!=Old.Revision)
    {R.Code=EAetherStoreCode::Corrupt;R.Detail=Why;return R;}
    if(!AetherWorldCodec::Decode(W.Value.Payload,D.Items,D.Rules,Index.Revisions,After,Why)||After.Revision!=W.Value.Revision)
    {R.Code=EAetherStoreCode::Invalid;R.Detail=Why;return R;}
    // 迁移来源是不可变审计事实，运行中保存不能重新声明或擦除来源。
    if(After.LegacySaveSchema!=Before.LegacySaveSchema||After.LegacyGeneration!=Before.LegacyGeneration||
        !After.LegacySourceSha256.Equals(Before.LegacySourceSha256,ESearchCase::CaseSensitive))
    {R.Code=EAetherStoreCode::Invalid;R.Detail=TEXT("Checkpoint cannot change migration provenance");return R;}
    FStatement Q(DB,"UPDATE aggregates SET revision=?,schema=?,payload=? WHERE kind=1 AND id='Main' AND revision=?");
    if(!Q.Int(1,W.Value.Revision)||!Q.Int(2,W.Value.SchemaVersion)||!Q.Blob(3,W.Value.Payload)||
        !Q.Int(4,W.ExpectedRevision)||Q.Step()!=SQLITE_DONE||sqlite3_changes(DB)!=1||!Tx.Commit())
    {R.Detail=Error(DB);return R;}
    R.Code=EAetherStoreCode::Committed;R.Value=W.Value;return R;
}
}
