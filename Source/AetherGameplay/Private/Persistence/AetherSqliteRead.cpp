#include "AetherSqliteInternal.h"
namespace AetherSQLite::Private
{
FAetherStoreRevisionIndex ReadRevisions(sqlite3* DB,EAetherAggregateKind Kind)
{
    FAetherStoreRevisionIndex R;
    const int32 Limit=Kind==EAetherAggregateKind::Profile?128:Kind==EAetherAggregateKind::World?1:4096;
    FStatement Q(DB,"SELECT id,revision,schema FROM aggregates WHERE kind=? ORDER BY id LIMIT ?");
    if(!Q.Int(1,uint8(Kind))||!Q.Int(2,Limit+1)){R.Detail=Error(DB);return R;}
    int Step=SQLITE_DONE;
    while((Step=Q.Step())==SQLITE_ROW)
    {
        const int32 Bytes=sqlite3_column_bytes(Q.Handle,0);const auto* Raw=sqlite3_column_text(Q.Handle,0);
        if(!Raw||Bytes<1||Bytes>512||R.Revisions.Num()>=Limit)
        {R.Code=EAetherStoreCode::Corrupt;R.Detail=TEXT("Aggregate index exceeds identity/count bounds");R.Revisions.Reset();return R;}
        FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Raw),Bytes);const FString Id(Decoded.Length(),Decoded.Get());FTCHARToUTF8 Back(*Id);
        bool Valid=!Id.IsEmpty()&&Id.Len()<=128&&Back.Length()==Bytes&&FMemory::Memcmp(Back.Get(),Raw,Bytes)==0;
        for(TCHAR C:Id)Valid&=C>=32;
        const int64 Revision=Q.ColumnInt(1);
        if(!Valid||Revision<0||Revision==MAX_int64||Q.ColumnInt(2)!=Schema||R.Revisions.Contains(Id))
        {R.Code=EAetherStoreCode::Corrupt;R.Detail=TEXT("Invalid aggregate revision index");R.Revisions.Reset();return R;}
        R.Revisions.Add(Id,Revision);
    }
    if(Step!=SQLITE_DONE){R.Detail=Error(DB);R.Revisions.Reset();return R;}
    R.Code=EAetherStoreCode::Found;return R;
}
FAetherStoreSnapshotResult ReadSnapshot(sqlite3* DB,const FAetherStoreSnapshotQuery& Query)
{
    FAetherStoreSnapshotResult R;FTransactionGuard Tx(DB,true);
    const auto Fail=[&](EAetherStoreCode Code,const FString& Detail)
    {R.Code=Code;R.Detail=Detail;R.Values.Reset();R.ProfileRevisions.Reset();R.ContainerRevisions.Reset();R.WorldRevisions.Reset();R.ContainerCount=-1;return R;};
    if(!Tx.Active)return Fail(EAetherStoreCode::Unavailable,Error(DB));
    int64 Bytes=0;
    for(const auto& K:Query.Keys)
    {
        auto Value=ReadAggregate(DB,K);
        if(Value.Code==EAetherStoreCode::Missing)continue;
        if(Value.Code!=EAetherStoreCode::Found||!Value.Value.IsSet())return Fail(Value.Code,Value.Detail);
        Bytes+=Value.Value->Payload.Num();
        if(Bytes>8*1024*1024)return Fail(EAetherStoreCode::Invalid,TEXT("Aggregate snapshot exceeds total payload bound"));
        R.Values.Add(K,MoveTemp(Value.Value.GetValue()));
    }
    if(Query.bIncludeProfileRevisions)
    {
        auto Index=ReadRevisions(DB,EAetherAggregateKind::Profile);
        if(Index.Code!=EAetherStoreCode::Found)return Fail(Index.Code,Index.Detail);
        R.ProfileRevisions=MoveTemp(Index.Revisions);
    }
    if(Query.bIncludeWorldRevisions)
    {
        auto Index=ReadRevisions(DB,EAetherAggregateKind::World);
        if(Index.Code!=EAetherStoreCode::Found)return Fail(Index.Code,Index.Detail);
        R.WorldRevisions=MoveTemp(Index.Revisions);
    }
    if(Query.bIncludeContainerRevisions)
    {
        auto Index=ReadRevisions(DB,EAetherAggregateKind::Container);
        if(Index.Code!=EAetherStoreCode::Found)return Fail(Index.Code,Index.Detail);
        R.ContainerRevisions=MoveTemp(Index.Revisions);
    }
    if(Query.bIncludeContainerCount)
    {
        FStatement Q(DB,"SELECT count(*) FROM aggregates WHERE kind=2");
        if(Q.Step()!=SQLITE_ROW)return Fail(EAetherStoreCode::Unavailable,Error(DB));
        const int64 Count=Q.ColumnInt(0);
        if(Count<0||Count>4096)return Fail(EAetherStoreCode::Corrupt,TEXT("Container registry exceeds bounded capacity"));
        R.ContainerCount=int32(Count);
    }
    if(!Tx.Commit())return Fail(EAetherStoreCode::Unavailable,Error(DB));
    R.Code=EAetherStoreCode::Found;return R;
}
}
