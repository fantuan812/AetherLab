#pragma once
#include "CoreMinimal.h"
#include "AetherStartupState.generated.h"

UENUM()
enum class EAetherStartupStage:uint8
{
    Idle,Frontend,Connecting,WaitingForBackend,ReadingStorage,Auditing,Restoring,WorldReady,Ready,Failed,Cancelled
};
UENUM()
enum class EAetherStartupFailure:uint8
{
    None,InvalidConfiguration,BackendConflict,BackendDrainTimedOut,StorageOpenFailed,StorageAuditFailed,
    WorldRestoreFailed,ProfileUnavailable,NetworkFailure,TravelFailure
};
// 只复制有界阶段，不发送保存前缀、路径、原始错误、账号或重试目的地。
USTRUCT()
struct AETHERGAMEPLAY_API FAetherStartupSnapshot
{
    GENERATED_BODY()
    UPROPERTY() FGuid AttemptId;
    UPROPERTY() uint32 Sequence=0;
    UPROPERTY() EAetherStartupStage Stage=EAetherStartupStage::Idle;
    UPROPERTY() EAetherStartupFailure FailureCode=EAetherStartupFailure::None;
};

// 本地路由能力与服务端失败分开；缺少前端包不能覆盖真实的存储失败原因。
enum class EAetherStartupRouteIssue:uint8
{None,FrontendUnavailable,PlayableUnavailable,NoRetryTarget,NoLocalWorld,Busy};
struct AETHERGAMEPLAY_API FAetherStartupView
{
    FGuid LocalAttemptToken,ServerAttemptId;
    EAetherStartupStage Stage=EAetherStartupStage::Idle;
    EAetherStartupFailure FailureCode=EAetherStartupFailure::None;
    bool bCanStart=false,bCanCancel=false,bCanRetry=false;
    EAetherStartupRouteIssue StartIssue=EAetherStartupRouteIssue::Busy;
    EAetherStartupRouteIssue CancelIssue=EAetherStartupRouteIssue::Busy;
    EAetherStartupRouteIssue RetryIssue=EAetherStartupRouteIssue::Busy;
};
namespace AetherStartup
{
    AETHERGAMEPLAY_API bool SameView(const FAetherStartupView& Before,const FAetherStartupView& After);
}
