#pragma once
#include "CoreMinimal.h"

// 地图烘焙和运行时装配共享同一份基础几何定义；不引用任何编辑器类型。
struct FAetherShellPiece
{
    FName Id;
    FVector Location;
    FVector Scale;
    bool Spatial = true;
};

namespace AetherShell
{
    // 只读进程级数据。运行时据此跳过已经烘焙的静态外壳，避免叠加碰撞。
    AETHERCORE_API const TArray<FAetherShellPiece>& Pieces();
    AETHERCORE_API bool IsShellPiece(FName Id);
}
