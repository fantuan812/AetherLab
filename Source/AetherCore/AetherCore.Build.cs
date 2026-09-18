using UnrealBuildTool;

// 定义、协议与值规则模块：禁止引入 Character、GAS、UMG、Editor 或原生推理依赖。
public class AetherCore : ModuleRules
{
    public AetherCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        PublicDependencyModuleNames.Add("Core");
        PrivateDependencyModuleNames.Add("Json");
    }
}
