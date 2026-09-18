using UnrealBuildTool;

// 定义、协议、持久 DTO 与值规则模块：禁止引入 Character、GAS、UMG、Editor 或原生推理依赖。
public class AetherCore : ModuleRules
{
    public AetherCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false; // 任务成长及来源回归、交互查询与持久 DTO独立编译，保留模块边界检查。
        PublicDependencyModuleNames.Add("Core");
        PrivateDependencyModuleNames.Add("Json");
    }
}
