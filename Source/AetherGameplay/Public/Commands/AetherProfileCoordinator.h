#pragma once
#include "Commands/AetherProfileCommand.h"
#include "Contracts/AetherTransactionalStore.h"
#include "World/AetherWorldState.h"
#include "World/AetherContainerState.h"
#include "Quests/AetherQuestProgression.h"

struct FAetherProfileSession
{
    FString CharacterId;
    FGuid SessionId;
    uint64 PawnEpoch=0;
    bool operator==(const FAetherProfileSession& Other) const
    {return CharacterId.Equals(Other.CharacterId,ESearchCase::CaseSensitive)&&SessionId==Other.SessionId&&PawnEpoch==Other.PawnEpoch;}
};
// 仅服务器内部的提交证明。客户端业务结果无法区分“提交失败”和“提交成功但刷新失败”。
enum class EAetherCommitCertainty:uint8 {NotSubmitted,NotCommitted,Committed,Unknown};
struct FAetherProfileCompletion
{
    FAetherProfileSession Session;
    EAetherCommandType CommandType=EAetherCommandType::Invalid;
    EAetherCommitCertainty CommitCertainty=EAetherCommitCertainty::NotSubmitted;
    bool bReservedResources=false;
    FAetherCommandResult Result;
    // 这是磁盘已提交的最新状态，不是仅在内存计算的候选；重放时版本可高于原回执。
    TOptional<FAetherProfileStateV10> Snapshot;
    TOptional<FAetherWorldStateV10> WorldSnapshot;
    TOptional<FAetherContainerStateV10> ContainerSnapshot;
    // 消费者真正应用前还须 IsCurrent(Session)，防止 Poll 返回后同帧又更换 Pawn。
    bool bMayPublish=false;
};
using FAetherResolveProfileContext=TFunction<bool(const FAetherProfileSession&,const FAetherPlayerCommand&,const FAetherProfileStateV10&,FAetherProfileCommandContext&)>;

// 游戏线程拥有；Poll 只检查就绪 Future，不阻塞等待磁盘，也不在 writer 线程触碰 Actor。
// 网络连接鉴权仍由调用者完成，BeginSession 不从客户端载荷提取身份。
class AETHERGAMEPLAY_API FAetherProfileCoordinator
{
public:
    FAetherProfileCoordinator(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store,
        FAetherV10ItemDefinitions Items,FAetherSkillDefinitionsV10 Skills,FAetherRules Rules,FAetherEconomyDefinitionsV10 Economy={},
        FAetherInteractionDefinitions Interactions={},FAetherQuestProgressionDefinitions Progression={});
    ~FAetherProfileCoordinator();
    FAetherProfileSession BeginSession(const FString& ServerCharacterId);
    FAetherProfileSession ReplacePawn(const FAetherProfileSession& Session);
    void EndSession(const FAetherProfileSession& Session);
    bool IsCurrent(const FAetherProfileSession& Session) const;
    bool Submit(const FAetherProfileSession& Session,const FAetherPlayerCommand& Command,FAetherCommandResult& Rejection);
    // Resolve 在读到当前角色后、构建提交前执行；它不得重入协调者的可写 API。
    TArray<FAetherProfileCompletion> Poll(const FAetherResolveProfileContext& Resolve);
    int32 PendingCount() const;
    bool HasPendingForCharacter(const FString& CharacterId) const;
private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
