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
    // 只验证有界局部段的碰撞及起终支撑（高差不超过 CMC.MaxStepHeight），不证明整路径可达，不移动角色。
    AETHERGAMEPLAY_API bool IsLocalStepClear(const ACharacter& Character,FVector Delta,FHitResult& OutFloor);
}
