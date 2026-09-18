#pragma once
#include "CoreMinimal.h"

// 聚合是并发控制边界。库存、成长和任务共享角色聚合版本，不能各自提交后再拼接。
enum class EAetherAggregateKind : uint8 { Profile, World, Container };
enum class EAetherStoreCode : uint8
{
    Ready, Found, Missing, Committed, Replayed, Conflict, Expired, Invalid, Busy, Unavailable, Corrupt, UnsupportedSchema
};

struct FAetherAggregateKey
{
    EAetherAggregateKind Kind = EAetherAggregateKind::Profile;
    FString Id;
    bool operator==(const FAetherAggregateKey& Other) const { return Kind == Other.Kind && Id == Other.Id; }
};
inline uint32 GetTypeHash(const FAetherAggregateKey& Key) { return HashCombine(GetTypeHash(uint8(Key.Kind)), GetTypeHash(Key.Id)); }

struct FAetherStoredAggregate
{
    FAetherAggregateKey Key;
    int64 Revision = 0;
    int32 SchemaVersion = 10;
    TArray<uint8> Payload;
};
struct FAetherAggregateWrite
{
    FAetherStoredAggregate Value;
    // -1 表示必须不存在；已有记录必须准确匹配版本，然后只推进一次。
    int64 ExpectedRevision = -1;
};

// 投递与聚合同时持久提交。消费者必须按 Id 幂等恢复，确认后由存储删除待投递项。
// Payload 是版本化结果（如目标生命值），不得把可重复累加的“治疗 +50”当成恢复事实。
struct FAetherEffectDelivery
{
    FGuid Id;
    FString ActorId;
    int32 SchemaVersion = 1;
    TArray<uint8> Payload;
};

struct FAetherTransaction
{
    FGuid CommandId;
    // 来自服务器连接解析，存储不会从客户端载荷推断身份。
    FString ActorId;
    int32 ProtocolVersion = 1;
    // 明确编码的完整请求，禁止依赖 FName 内部序号或进程地址作持久哈希。
    TArray<uint8> Request;
    TArray<uint8> Result;
    TArray<FAetherAggregateWrite> Writes;
    TArray<FAetherEffectDelivery> Effects;
    // 去重窗口淘汰后，命令仍必须携带当前角色版本才能执行。
    int64 ExpectedProfileRevision = -1;
};

struct FAetherStoreResult
{
    EAetherStoreCode Code = EAetherStoreCode::Unavailable;
    int64 FinalProfileRevision = -1;
    TArray<uint8> Result;
    FString Detail;
};

// 仅约束 DTO 形状；距离、持有权、物品规则等必须由权威用例校验。
namespace AetherTransactions
{
    inline constexpr int32 SchemaVersion = 10;
    inline constexpr int32 ProtocolVersion = 1;
    inline constexpr int32 ReceiptLimit = 64;
    inline constexpr int32 MaxPayloadBytes = 4 * 1024 * 1024;
    // ID 高 64 位绑定期望版本；低 64 位随机。这样回执淘汰后也不能以旧 ID 配新版本。
    AETHERCORE_API FGuid NewCommandId(int64 ExpectedProfileRevision);
    AETHERCORE_API bool Validate(const FAetherTransaction& Transaction, FString& Reason);
}
