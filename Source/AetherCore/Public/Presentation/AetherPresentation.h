#pragma once
#include "CoreMinimal.h"
class UClass;

// 客户端模块注册工厂，玩法只请求本地表现类；Core 无需链接 Engine/UMG。
// 所有调用都在游戏线程，Shutdown 必须注销，避免持有已卸载 DLL 的函数地址。
namespace AetherPresentation
{
    using FHUDFactory = UClass*(*)();
    AETHERCORE_API void RegisterHUD(FHUDFactory Factory);
    AETHERCORE_API void UnregisterHUD(FHUDFactory Factory);
    AETHERCORE_API UClass* ResolveHUD();
}
