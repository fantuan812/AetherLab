#pragma once
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Startup/AetherStartupState.h"
#include "AetherStartupClient.generated.h"

class AAetherPlayerController;
struct FAetherStartupClientImpl;
DECLARE_MULTICAST_DELEGATE(FOnAetherStartupViewChanged);

// UI 的唯一只读启动入口。当前 GI 拥有本地 attempt、目标和受限路由；没有服务端取消 RPC。
UCLASS()
class AETHERGAMEPLAY_API UAetherStartupClient : public UGameInstanceSubsystem,public FTickableGameObject
{
    GENERATED_BODY()
public:
    UAetherStartupClient();
    virtual ~UAetherStartupClient() override;
    const FAetherStartupView& GetView() const;
    FOnAetherStartupViewChanged OnChanged;
    // 调用者冻结按下时 token；拒绝过期 token 不改变后来建立的 attempt。
    bool RequestStart(FGuid LocalAttemptToken);
    bool RequestCancel(FGuid LocalAttemptToken);
    bool RequestRetry(FGuid LocalAttemptToken);
    // 仅供拥有者 RPC；仍复验当前 GI、Controller、World、attempt 与序号。
    void ReceiveStartup(AAetherPlayerController* Controller,const FAetherStartupSnapshot& Snapshot);
    // CommandClient 接受拥有者通道后通知；UI 不可把旧 Controller 的缓存当作当前角色就绪。
    void ObserveCommandChannel(AAetherPlayerController* Controller,FGuid Channel);
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;
private:
    friend class FAetherStartupLifecycleTest;
    friend struct FAetherStartupClientImpl;
    bool OwnsCommandChannel(AAetherPlayerController* Controller,FGuid Channel) const;
    // 路由/通知栈持有局部共享引用，Deinitialize 撤下成员也不释放正在执行的栈帧。
    TSharedPtr<FAetherStartupClientImpl> Impl;
};
