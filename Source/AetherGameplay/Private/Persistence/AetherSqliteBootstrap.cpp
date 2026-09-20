#include "AetherSqliteInternal.h"

namespace AetherSQLite::Private
{
bool ValidInitialAggregate(const FAetherStoredAggregate& V,EAetherAggregateKind Kind)
{
    if(V.Key.Kind!=Kind||V.Revision!=0||V.SchemaVersion!=10||V.Payload.IsEmpty()||V.Payload.Num()>AetherTransactions::MaxPayloadBytes)return false;
    if(Kind==EAetherAggregateKind::World)return V.Key.Id.Equals(TEXT("Main"),ESearchCase::CaseSensitive);
    if(Kind!=EAetherAggregateKind::Profile||V.Key.Id.IsEmpty()||V.Key.Id.Len()>32)return false;
    for(TCHAR C:V.Key.Id)if(C<32)return false;
    FTCHARToUTF8 U(*V.Key.Id);FUTF8ToTCHAR Back(U.Get(),U.Length());
    return V.Key.Id.Equals(FString(Back.Length(),Back.Get()),ESearchCase::CaseSensitive);
}
namespace
{
bool Insert(sqlite3* DB,const FAetherStoredAggregate& V)
{
    // 明确禁止 UPSERT：首次创建不能因竞态覆盖另一个连接刚创建的角色。
    FStatement Q(DB,"INSERT INTO aggregates(kind,id,revision,schema,payload) VALUES(?,?,?,?,?)");
    return Q.Int(1,uint8(V.Key.Kind))&&Q.Text(2,V.Key.Id)&&Q.Int(3,V.Revision)&&Q.Int(4,V.SchemaVersion)&&Q.Blob(5,V.Payload)&&Q.Step()==SQLITE_DONE;
}
}
FAetherStoreResult InitializeWorld(sqlite3* DB,const FAetherStoredAggregate& World)
{
    FAetherStoreResult R;
    if(!ValidInitialAggregate(World,EAetherAggregateKind::World)){R.Code=EAetherStoreCode::Invalid;return R;}
    FTransactionGuard Tx(DB);
    if(!Tx.Active){R.Code=EAetherStoreCode::Busy;R.Detail=Error(DB);return R;}
    const auto Existing=ReadAggregate(DB,World.Key);
    if(Existing.Code==EAetherStoreCode::Found)
    {
        // 初始化响应丢失允许查询同一份原始世界；已推进的世界绝不重置。
        R.Code=Existing.Value.IsSet()&&Existing.Value->Revision==0&&Existing.Value->Payload==World.Payload?EAetherStoreCode::Replayed:EAetherStoreCode::Conflict;
        return R;
    }
    if(Existing.Code!=EAetherStoreCode::Missing){R.Code=Existing.Code;R.Detail=Existing.Detail;return R;}
    FStatement Count(DB,"SELECT (SELECT count(*) FROM aggregates)+(SELECT count(*) FROM receipts)+(SELECT count(*) FROM effects)");
    if(Count.Step()!=SQLITE_ROW){R.Detail=Error(DB);return R;}
    if(Count.ColumnInt(0)!=0){R.Code=EAetherStoreCode::Conflict;R.Detail=TEXT("Missing world in nonempty database; refusing blank replacement");return R;}
    if(!Insert(DB,World)||!Tx.Commit()){R.Detail=Error(DB);return R;}
    R.Code=EAetherStoreCode::Committed;return R;
}
FAetherStoreReadResult CreateProfile(sqlite3* DB,const FAetherStoredAggregate& Profile)
{
    FAetherStoreReadResult R;
    if(!ValidInitialAggregate(Profile,EAetherAggregateKind::Profile)){R.Code=EAetherStoreCode::Invalid;return R;}
    FTransactionGuard Tx(DB);
    if(!Tx.Active){R.Code=EAetherStoreCode::Busy;R.Detail=Error(DB);return R;}
    const auto World=ReadAggregate(DB,{EAetherAggregateKind::World,TEXT("Main")});
    if(World.Code!=EAetherStoreCode::Found){R.Code=World.Code;R.Detail=TEXT("World must exist before profile creation");return R;}
    // SQLite 默认二进制键与领域的大小写比较不同；在同一个写事务里显式拒绝别名。
    FStatement Ids(DB,"SELECT id FROM aggregates WHERE kind=0 LIMIT 129");int Count=0,Step=0;
    while((Step=Ids.Step())==SQLITE_ROW)
    {
        ++Count;const FString Id=Ids.ColumnText(0);
        if(Id.Equals(Profile.Key.Id,ESearchCase::IgnoreCase)&&!Id.Equals(Profile.Key.Id,ESearchCase::CaseSensitive))
        {R.Code=EAetherStoreCode::Conflict;R.Detail=TEXT("Character identity has a case alias");return R;}
    }
    if(Step!=SQLITE_DONE){R.Detail=Error(DB);return R;}
    const auto Existing=ReadAggregate(DB,Profile.Key);
    if(Existing.Code==EAetherStoreCode::Found)return Existing; // 重连/竞态只读取现有记录，不再次发初始物品。
    if(Existing.Code!=EAetherStoreCode::Missing)return Existing;
    if(Count>=128){R.Code=EAetherStoreCode::Conflict;R.Detail=TEXT("Profile capacity reached");return R;}
    if(!Insert(DB,Profile)||!Tx.Commit()){R.Detail=Error(DB);return R;}
    R.Code=EAetherStoreCode::Found;R.Value=Profile;return R;
}
}
