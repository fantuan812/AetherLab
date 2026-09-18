#pragma once
#include "Persistence/AetherSqliteStore.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Event.h"
#include "Misc/ScopeLock.h"
#include "Containers/Queue.h"
#include "Templates/Function.h"
#include "SQLite/AetherSqlitePrefix.h"
THIRD_PARTY_INCLUDES_START
#include "SQLite/sqlite3.h"
THIRD_PARTY_INCLUDES_END

namespace AetherSQLite::Private
{
    inline constexpr int32 ApplicationId = 0x41453130; // AE10
    inline constexpr int32 Schema = 10;
    inline FString Error(sqlite3* DB) { return DB ? UTF8_TO_TCHAR(sqlite3_errmsg(DB)) : TEXT("Database unavailable"); }
    inline bool Exec(sqlite3* DB, const char* SQL) { return sqlite3_exec(DB, SQL, nullptr, nullptr, nullptr) == SQLITE_OK; }

    // 每条语句独占 prepared statement；全部用户数据都 bind，绝不拼接 SQL。
    struct FStatement
    {
        sqlite3_stmt* Handle = nullptr;
        FStatement(sqlite3* DB, const char* SQL) { sqlite3_prepare_v2(DB, SQL, -1, &Handle, nullptr); }
        ~FStatement() { if (Handle) sqlite3_finalize(Handle); }
        FStatement(const FStatement&) = delete;
        bool Valid() const { return Handle != nullptr; }
        bool Text(int Index, const FString& Value)
        {
            FTCHARToUTF8 UTF8(*Value);
            return Handle && sqlite3_bind_text(Handle, Index, UTF8.Get(), UTF8.Length(), SQLITE_TRANSIENT) == SQLITE_OK;
        }
        bool Int(int Index, int64 Value) { return Handle && sqlite3_bind_int64(Handle, Index, Value) == SQLITE_OK; }
        bool Blob(int Index, const TArray<uint8>& Value)
        {
            // 空结果也是 blob，不能被 NULL 破坏 NOT NULL 约束。
            return Handle && sqlite3_bind_blob(Handle, Index, Value.IsEmpty() ? "" : static_cast<const void*>(Value.GetData()), Value.Num(), SQLITE_TRANSIENT) == SQLITE_OK;
        }
        int Step() { return Handle ? sqlite3_step(Handle) : SQLITE_ERROR; }
        int64 ColumnInt(int Index) const { return sqlite3_column_int64(Handle, Index); }
        FString ColumnText(int Index) const
        {
            const auto* Data = sqlite3_column_text(Handle, Index);
            return Data ? UTF8_TO_TCHAR(reinterpret_cast<const char*>(Data)) : FString();
        }
        bool ColumnBlob(int Index, TArray<uint8>& Out, int32 Limit = AetherTransactions::MaxPayloadBytes) const
        {
            if (sqlite3_column_type(Handle, Index) != SQLITE_BLOB) return false;
            const int Bytes = sqlite3_column_bytes(Handle, Index);
            if (Bytes < 0 || Bytes > Limit) return false;
            Out.Reset(Bytes);
            if (Bytes) Out.Append(static_cast<const uint8*>(sqlite3_column_blob(Handle, Index)), Bytes);
            return true;
        }
    };

    // 任何失败路径都回滚；只有明确 COMMIT 成功才解除回滚守卫。
    struct FTransactionGuard
    {
        sqlite3* DB;
        bool Active;
        explicit FTransactionGuard(sqlite3* InDB,bool ReadOnly=false) : DB(InDB), Active(Exec(DB, ReadOnly?"BEGIN":"BEGIN IMMEDIATE")) {}
        ~FTransactionGuard() { if (Active) Exec(DB, "ROLLBACK"); }
        bool Commit() { if (!Active || !Exec(DB, "COMMIT")) return false; Active = false; return true; }
    };

    FAetherStoreSnapshotResult ReadSnapshot(sqlite3* DB,const FAetherStoreSnapshotQuery& Query);
    FAetherStoreRevisionIndex ReadRevisions(sqlite3* DB,EAetherAggregateKind Kind);
    FAetherStoreReadResult ReadAggregate(sqlite3* DB, const FAetherAggregateKey& Key);
    FAetherStoreResult LookupReceipt(sqlite3* DB,const FAetherReceiptQuery& Query);
    FAetherStoreResult CommitTransaction(sqlite3* DB, const FAetherTransaction& Transaction, const FAetherSqliteOptions& Options);
    FAetherStoreResult ImportLegacy(sqlite3* DB,const FAetherLegacyImport& Import,const FAetherSqliteOptions& Options);
    FAetherStoreEffectsResult ReadEffects(sqlite3* DB, const FString& Actor);
    bool MakeBackup(sqlite3* DB, const FString& Path);
}
