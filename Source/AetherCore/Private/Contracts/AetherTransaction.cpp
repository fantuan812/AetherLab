#include "Contracts/AetherTransaction.h"
#include "Contracts/AetherTransactionalStore.h"
#include "Contracts/AetherPlayerCommand.h"

IAetherTransactionalStore::IAetherTransactionalStore() = default;
IAetherTransactionalStore::~IAetherTransactionalStore() = default;

TFuture<FAetherStoreResult> IAetherTransactionalStore::InitializeWorld(FAetherStoredAggregate)
{
    TPromise<FAetherStoreResult> P;auto F=P.GetFuture();FAetherStoreResult R;
    R.Code=EAetherStoreCode::UnsupportedSchema;R.Detail=TEXT("Backend does not support world initialization");P.SetValue(MoveTemp(R));return F;
}
TFuture<FAetherStoreReadResult> IAetherTransactionalStore::CreateProfile(FAetherStoredAggregate)
{
    TPromise<FAetherStoreReadResult> P;auto F=P.GetFuture();FAetherStoreReadResult R;
    R.Code=EAetherStoreCode::UnsupportedSchema;R.Detail=TEXT("Backend does not support profile creation");P.SetValue(MoveTemp(R));return F;
}

TFuture<FAetherStoreReadResult> IAetherTransactionalStore::CompareExchangeWorld(FAetherAggregateWrite)
{
    TPromise<FAetherStoreReadResult> P;auto F=P.GetFuture();FAetherStoreReadResult R;
    R.Code=EAetherStoreCode::UnsupportedSchema;R.Detail=TEXT("Backend does not support world checkpoints");P.SetValue(MoveTemp(R));return F;
}

FGuid AetherTransactions::NewCommandId(int64 ExpectedProfileRevision)
{
    check(ExpectedProfileRevision >= -1 && ExpectedProfileRevision < MAX_int64);
    const uint64 Version = uint64(ExpectedProfileRevision + 1);
    FGuid Random;
    do { Random = FGuid::NewGuid(); } while (Random.C == 0 && Random.D == 0);
    return FGuid(uint32(Version >> 32), uint32(Version), Random.C, Random.D);
}

bool AetherTransactions::Validate(const FAetherTransaction& T, FString& Reason)
{
    const auto Reject = [&Reason](const TCHAR* Text) { Reason = Text; return false; };
    if (!T.CommandId.IsValid() || T.ActorId.IsEmpty() || T.ActorId.Len() > 128)
        return Reject(TEXT("Invalid command or server actor identity"));
    if (!AetherCommands::IsSupportedProtocol(T.ProtocolVersion) || T.ExpectedProfileRevision < -1 || T.ExpectedProfileRevision == MAX_int64)
        return Reject(TEXT("Unsupported protocol or invalid profile revision"));
    const uint64 BoundRevision = (uint64(T.CommandId.A) << 32) | T.CommandId.B;
    if (BoundRevision != uint64(T.ExpectedProfileRevision + 1))
        return Reject(TEXT("Command identity is bound to a different profile revision"));
    if (T.Request.IsEmpty() || T.Request.Num() > 16384 || T.Result.Num() > 16384)
        return Reject(TEXT("Invalid request or result size"));
    if (T.Writes.IsEmpty() || T.Writes.Num() > 32 || T.Effects.Num() > 16)
        return Reject(TEXT("Invalid transaction bounds"));

    TSet<FAetherAggregateKey> Keys;
    bool HasActorProfile = false;
    int64 TotalPayload = 0;
    for (const auto& Write : T.Writes)
    {
        const auto& V = Write.Value;
        if (uint8(V.Key.Kind) > uint8(EAetherAggregateKind::Container) || V.Key.Id.IsEmpty() || V.Key.Id.Len() > 128 || Keys.Contains(V.Key))
            return Reject(TEXT("Invalid or duplicate aggregate key"));
        Keys.Add(V.Key);
        TotalPayload += V.Payload.Num();
        if (TotalPayload > 8 * 1024 * 1024) return Reject(TEXT("Transaction payload exceeds memory budget"));
        if (V.SchemaVersion != SchemaVersion || V.Payload.IsEmpty() || V.Payload.Num() > MaxPayloadBytes)
            return Reject(TEXT("Unsupported schema or invalid aggregate payload"));
        if (Write.ExpectedRevision < -1 || Write.ExpectedRevision == MAX_int64 || V.Revision != Write.ExpectedRevision + 1)
            return Reject(TEXT("Aggregate revision must advance exactly once"));
        if (V.Key.Kind == EAetherAggregateKind::Profile && V.Key.Id.Equals(T.ActorId,ESearchCase::CaseSensitive))
        {
            HasActorProfile = true;
            if (Write.ExpectedRevision != T.ExpectedProfileRevision)
                return Reject(TEXT("Profile command and write versions differ"));
        }
    }
    // 每个改变资产的请求都推进提交者版本，详细回执过期后也不能重新执行旧请求。
    if (!HasActorProfile) return Reject(TEXT("Transaction requires the actor profile"));

    TSet<FGuid> Effects;
    for (const auto& Effect : T.Effects)
    {
        if (!Effect.Id.IsValid() || Effect.Id.A != T.CommandId.A || Effect.Id.B != T.CommandId.B || Effects.Contains(Effect.Id) || !Effect.ActorId.Equals(T.ActorId,ESearchCase::CaseSensitive)
            || Effect.SchemaVersion != 1 || Effect.Payload.IsEmpty() || Effect.Payload.Num() > 16384)
            return Reject(TEXT("Invalid effect delivery"));
        Effects.Add(Effect.Id);
    }
    Reason.Reset();
    return true;
}
