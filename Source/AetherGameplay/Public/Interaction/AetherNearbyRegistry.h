#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/SceneComponent.h"
#include "AetherNearbyRegistry.generated.h"

// 每个世界（包括客户端）独立维护附近对象索引。只保存弱引用，不阻止区域卸载。
// Actor 在 BeginPlay/EndPlay 注册与注销，根组件移动时重分桶，查询无需扫描全世界。
UCLASS()
class AETHERGAMEPLAY_API UAetherNearbyRegistry : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    bool Register(AActor* Actor);
    void Unregister(AActor* Actor);
    bool Contains(const AActor* Actor) const;
    TArray<TWeakObjectPtr<AActor>> Nearby(FVector Position,double Radius) const;
    virtual void Deinitialize() override;
private:
    struct FEntry { FIntVector Cell; TWeakObjectPtr<USceneComponent> Root; };
    TMap<TWeakObjectPtr<AActor>,FEntry> Entries;
    TMap<FIntVector,TSet<TWeakObjectPtr<AActor>>> Cells;
    void Moved(USceneComponent* Component,EUpdateTransformFlags Flags,ETeleportType Teleport);
    void RemoveFromCell(TWeakObjectPtr<AActor> Actor,FIntVector Cell);
    static bool CellFor(FVector Position,FIntVector& Out);
};
