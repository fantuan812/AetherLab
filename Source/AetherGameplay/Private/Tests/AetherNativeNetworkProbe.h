#pragma once
class AAetherPlayerController;
// 仅显式开发验收参数启用；正式 RPC 和持久后端仍是唯一写入路径。
namespace AetherNativeNetworkProbe { void Tick(AAetherPlayerController* Controller); }
