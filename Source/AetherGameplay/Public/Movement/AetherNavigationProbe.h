#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class ACharacter;
struct FHitResult;

struct FAetherNavigationProbeGeometry
{
    FVector Center=FVector::ZeroVector;
    FQuat Rotation=FQuat::Identity;
    float Radius=0,HalfHeight=0;
    ECollisionChannel Channel=ECC_Pawn;
};

namespace AetherNavigationProbe
{
    // 每次读取当前组件，不保存默认尺寸，也不缓存蹲伏前的几何。
    AETHERGAMEPLAY_API bool ReadGeometry(const ACharacter& Character,FAetherNavigationProbeGeometry& Out);
    // 只验证局部候选段的碰撞/地面支撑，不证明整个导航路径可达，不移动角色。
    AETHERGAMEPLAY_API bool IsLocalStepClear(const ACharacter& Character,FVector Delta,FHitResult& OutFloor);
}
