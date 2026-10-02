#pragma once
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AetherStartupPresentationSubsystem.generated.h"

class UAetherStartupClient;
class UAetherStartupOverlay;
class UGameViewportClient;
class ULocalPlayer;
class APlayerController;

// 只管理本 GI 的展示生命周期。attempt、阶段和路由能力均由 Gameplay 客户端服务提供。
UCLASS()
class AETHERUI_API UAetherStartupPresentationSubsystem : public UGameInstanceSubsystem,public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsTickable() const override;
    virtual bool IsTickableWhenPaused() const override {return true;}
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;
private:
    friend class FAetherStartupPresentationTest;
    void ViewChanged();
    void RefreshPresentation();
    void ReleasePresentation();
    TWeakObjectPtr<UAetherStartupClient> Client;
    TWeakObjectPtr<UWorld> BoundWorld;
    TWeakObjectPtr<UGameViewportClient> BoundViewport;
    TWeakObjectPtr<ULocalPlayer> BoundPlayer;
    TWeakObjectPtr<APlayerController> BoundController;
    UPROPERTY(Transient) TObjectPtr<UAetherStartupOverlay> Overlay;
    bool bViewDirty=true;
};
