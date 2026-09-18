#include "AetherSqliteInternal.h"
#include "Hash/Blake3.h"
#include "HAL/PlatformMisc.h"
namespace AetherSQLite::Private
{
namespace
{
TArray<uint8> Marker(const FAetherLegacyImport& Import)
{
    FBlake3 Hash;
    const auto Number=[&](uint64 V,int32 Bytes){uint8 B[8];for(int32 I=0;I<Bytes;++I)B[I]=uint8(V>>(8*I));Hash.Update(B,Bytes);};
    const auto Text=[&](const FString& S){FTCHARToUTF8 V(*S);Number(V.Length(),4);Hash.Update(V.Get(),V.Length());};
    Number(1,1);Number(Import.SourceSchema,1);Text(Import.SourceSha256);Number(Import.Values.Num(),4);
    TArray<const FAetherStoredAggregate*> Values;for(const auto& V:Import.Values)Values.Add(&V);
    Values.Sort([](const auto& A,const auto& B){return A.Key.Kind!=B.Key.Kind?uint8(A.Key.Kind)<uint8(B.Key.Kind):A.Key.Id.Compare(B.Key.Id,ESearchCase::CaseSensitive)<0;});
    for(const auto* V:Values)
    {
        Number(uint8(V->Key.Kind),1);Text(V->Key.Id);Number(V->Revision,8);Number(V->SchemaVersion,4);
        Number(V->Payload.Num(),4);Hash.Update(V->Payload.GetData(),V->Payload.Num());
    }
    // 原文件 SHA256 与转换结果的 BLAKE3 同时绑定；同一来源哈希不能偷偷替换转换后的内容。
    TArray<uint8> Out={0x41,0x49,0x4D,0x50,1,uint8(Import.SourceSchema)};
    for(TCHAR C:Import.SourceSha256)Out.Add(uint8(C));
    const auto Digest=Hash.Finalize();Out.Append(Digest.GetBytes(),32);return Out;
}
}
FAetherStoreResult ImportLegacy(sqlite3* DB,const FAetherLegacyImport& Import,const FAetherSqliteOptions& Options)
{
    FAetherStoreResult Result;
    if(!AetherImports::Validate(Import,Result.Detail)){Result.Code=EAetherStoreCode::Invalid;return Result;}
    const auto Fail=[&](){Result.Code=EAetherStoreCode::Unavailable;Result.Detail=Error(DB);return Result;};
    FTransactionGuard Transaction(DB);
    if(!Transaction.Active)return Fail();
    const FAetherAggregateKey MarkerKey{EAetherAggregateKind::Migration,TEXT("LegacyV9")};
    const auto Payload=Marker(Import);
    auto Previous=ReadAggregate(DB,MarkerKey);
    if(Previous.Code==EAetherStoreCode::Found&&Previous.Value.IsSet())
    {
        if(Previous.Value->Payload!=Payload)
        {Result.Code=EAetherStoreCode::Conflict;Result.Detail=TEXT("Different source or conversion already imported");return Result;}
        if(Previous.Value->Revision!=0){Result.Code=EAetherStoreCode::Corrupt;Result.Detail=TEXT("Import marker version changed");return Result;}
        // 标识不是跳过完整性检查的理由。正常游戏可推进版本，但缺行/倒退不能报“已导入”。
        for(const auto& Expected:Import.Values)
        {
            const auto Current=ReadAggregate(DB,Expected.Key);
            if(Current.Code!=EAetherStoreCode::Found||!Current.Value.IsSet()||Current.Value->Revision<Expected.Revision||
                (Current.Value->Revision==Expected.Revision&&Current.Value->Payload!=Expected.Payload))
            {Result.Code=EAetherStoreCode::Corrupt;Result.Detail=TEXT("Imported aggregate is missing, regressed or changed without a new revision");return Result;}
        }
        Result.Code=EAetherStoreCode::Replayed;Result.Detail=TEXT("Legacy source already imported; current game state retained");return Result;
    }
    if(Previous.Code!=EAetherStoreCode::Missing){Result.Code=Previous.Code;Result.Detail=Previous.Detail;return Result;}
    {
        FStatement Count(DB,"SELECT (SELECT count(*) FROM aggregates)+(SELECT count(*) FROM receipts)+(SELECT count(*) FROM effects)");
        if(Count.Step()!=SQLITE_ROW)return Fail();
        if(Count.ColumnInt(0)!=0){Result.Code=EAetherStoreCode::Conflict;Result.Detail=TEXT("Import refuses a nonempty database");return Result;}
    }
#if WITH_DEV_AUTOMATION_TESTS
    const auto Fault=Options.Fault?Options.Fault->Exchange(EAetherStoreFault::None):EAetherStoreFault::None;
#endif
    const auto Insert=[&](const FAetherStoredAggregate& V)
    {
        FStatement Put(DB,"INSERT INTO aggregates(kind,id,revision,schema,payload) VALUES(?,?,?,?,?)");
        return Put.Int(1,uint8(V.Key.Kind))&&Put.Text(2,V.Key.Id)&&Put.Int(3,V.Revision)&&Put.Int(4,V.SchemaVersion)&&Put.Blob(5,V.Payload)&&Put.Step()==SQLITE_DONE;
    };
    int32 Written=0;
    for(const auto& V:Import.Values)
    {
        if(!Insert(V))return Fail();++Written;
#if WITH_DEV_AUTOMATION_TESTS
        if(Written==1&&Fault==EAetherStoreFault::AfterFirstWrite){Result.Detail=TEXT("Injected failure during import");return Result;}
#endif
    }
    FAetherStoredAggregate Record;Record.Key=MarkerKey;Record.Payload=Payload;
    if(!Insert(Record))return Fail();
#if WITH_DEV_AUTOMATION_TESTS
    if(Fault==EAetherStoreFault::CrashBeforeCommit)FPlatformMisc::RequestExitWithStatus(true,91);
    if(Fault==EAetherStoreFault::BeforeCommit){Result.Detail=TEXT("Injected failure before import commit");return Result;}
#endif
    if(!Transaction.Commit())return Fail();
#if WITH_DEV_AUTOMATION_TESTS
    if(Fault==EAetherStoreFault::CrashAfterCommit)FPlatformMisc::RequestExitWithStatus(true,92);
    if(Fault==EAetherStoreFault::AfterCommitBeforeReply){Result.Detail=TEXT("Injected lost import response");return Result;}
#endif
    Result.Code=EAetherStoreCode::Committed;return Result;
}
}
