#pragma once
#include "GameFramework/Actor.h"
#include "World/AetherContainerState.h"
#include "Persistence/AetherWorldBootstrap.h"
#include "AetherNativeContainer.generated.h"
class UStaticMeshComponent;

// 场景只复制容器身份/版本。库存内容必须经当前连接授权读取，不随 Actor 广播给附近玩家。
UCLASS()
class AETHERLAB_API AAetherNativeContainer : public AActor
{
    GENERATED_BODY()
public:
    AAetherNativeContainer();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(Replicated) FString StableId;
    UPROPERTY(Replicated) int64 Revision=-1;
    UPROPERTY(ReplicatedUsing=ApplyKind) uint8 ContainerKind=0;
    UFUNCTION() void ApplyKind();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    FString OwnerCharacterId;
    FString RegionId;
    bool Configure(const FAetherContainerRestoreDescriptor& Descriptor);
    bool Publish(const FAetherContainerStateV10& Committed);
    bool Allows(const FString& CharacterId) const;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
