#pragma once
#include "CoreMinimal.h"

// 数值属于网络协议，新增动作只能追加；禁止复用已发布值或用任意字符串执行。
enum class EAetherCommandType : uint8
{
    Invalid = 0, UseItem = 1, EquipItem = 2, UnequipItem = 3,
    SplitStack = 4, MergeStack = 5, BuyItem = 6, SellItem = 7,
    MoveItem = 8, SwapItems = 9, SetItemLock = 10, SortInventory = 11,
    DropItem = 12, PickUpItem = 13, TransferItem = 14, RepairItem = 15,
    LearnSkill = 16, UpgradeSkill = 17, ResetSkills = 18, BindSkill = 19,
    ExecuteInteraction = 20, ClaimReward = 21, SetItemFavorite = 22
};
enum class EAetherTransferDirection : uint8 { IntoContainer, FromContainer };
enum class EAetherCommandCode : uint8
{
    Applied, Replayed, Invalid, UnsupportedProtocol, UnsupportedAction,
    Unauthorized, StaleRevision, Expired, Conflict, Missing, NotAllowed,
    Capacity, InsufficientFunds, NotReady, OutOfReach, StorageUnavailable, Busy
};

// 信封没有 ActorIdentity、价格、效果强度、距离或客户端时间。
// 身份必须在服务器从拥有该 RPC 的连接/PlayerState 解析，规则值从服务端定义读取。
struct FAetherPlayerCommand
{
    uint16 ProtocolVersion = 1;
    FGuid CommandId;
    EAetherCommandType Type = EAetherCommandType::Invalid;
    int64 ExpectedProfileRevision = 0;
    int64 ExpectedWorldRevision = -1; // 不涉及世界聚合时必须为 -1。
    // 请求 v2 新增。ExecuteInteraction 必须携带；v1 和其他动作一律为 -1。
    int64 ExpectedInteractionRevision = -1;
    int64 ExpectedContainerRevision = -1; // v3 拾取/转移固定所见容器版本；其他动作必须 -1。
    FGuid ItemInstanceId, OtherInstanceId;
    FString TargetStableId, ContainerId, DefinitionId, SkillId, SlotId, ActionId;
    int32 Quantity = 0;
    int32 DestinationIndex = -1;
    bool Enabled = false; // 锁定/收藏的目标值；SortInventory 中表示是否合并相同堆。
    EAetherTransferDirection TransferDirection = EAetherTransferDirection::IntoContainer;
};

// 回执格式独立于请求版本；格式 2 保存转移关系，旧格式 1 回执保留只读兼容。
struct FAetherCommandTransfer
{
    FGuid From,To;
    int32 Quantity=0;
};
struct FAetherCommandResult
{
    FGuid CommandId;
    EAetherCommandCode Code = EAetherCommandCode::Invalid;
    int64 FinalProfileRevision = -1, FinalWorldRevision = -1;
    int32 ActualQuantity = 0;
    TArray<FGuid> AffectedIds;
    TArray<FAetherCommandTransfer> Transfers;
    TArray<FString> AffectedDefinitionIds; // 技能、任务等没有物品 GUID 的受影响对象。
    // 本地化按有限结果码选择文案；参数只传事实，UI 不解析任意可执行字符串。
    TMap<FString, FString> ReasonParameters;
};

namespace AetherCommands
{
    inline constexpr uint16 ProtocolVersion = 1; // 保留旧调用者默认值，不改变已保存请求。
    inline constexpr uint16 LatestProtocolVersion = 3;
    inline constexpr bool IsSupportedProtocol(int32 Version) { return Version==1||Version==2||Version==3; }
    inline constexpr int32 MaxWireBytes = 1024;
    inline constexpr uint16 ResultSchemaVersion = 2;
    // 这里只检查协议形状；是否持有物品、目标距离、权限和版本仍由权威处理器复验。
    AETHERCORE_API bool Validate(const FAetherPlayerCommand& Command, FString& Reason);
    // 使用固定小端整数和有界 ASCII 定义 ID；不写内存布局、FName 索引或 UObject 指针。
    // 编码后的同一份字节可作为 SQLite 回执的完整请求，重试不得重新填充 UI 状态。
    AETHERCORE_API bool Encode(const FAetherPlayerCommand& Command, TArray<uint8>& Bytes, FString& Reason);
    AETHERCORE_API bool Decode(const TArray<uint8>& Bytes, FAetherPlayerCommand& Command, FString& Reason);
    AETHERCORE_API bool ValidateResult(const FAetherCommandResult& Result, FString& Reason);
    AETHERCORE_API bool EncodeResult(const FAetherCommandResult& Result, TArray<uint8>& Bytes, FString& Reason);
    AETHERCORE_API bool DecodeResult(const TArray<uint8>& Bytes, FAetherCommandResult& Result, FString& Reason);
}
