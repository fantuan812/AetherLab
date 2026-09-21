param(
 [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
 [ValidateSet('CPU','Vulkan')][string]$Backend='CPU'
)
$ErrorActionPreference='Stop'
# 真模型用例单独显式选择，普通规则回归不隐式加载 700 MB 模型。
& (Join-Path $PSScriptRoot 'TestRules.ps1') -EngineRoot $EngineRoot -Filter "Aether.MotionIntegration.TrueNative.$Backend"
if($LASTEXITCODE -ne 0){throw 'UE native integration failed.'}
