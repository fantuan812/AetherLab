using UnrealBuildTool;
public class AetherUI : ModuleRules
{
    public AetherUI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        // 专服误引入界面依赖时立即报错，不允许悄悄带入渲染与输入组件。
        if (Target.Type == TargetType.Server) throw new BuildException("AetherUI is client-only.");
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UMG", "SlateCore", "Slate", "InputCore", "AetherLab" });
        // 旧调试 HUD 读取物理统计，正式页面迁移后再将此查询移入只读 Facade。
        PrivateDependencyModuleNames.AddRange(new[] { "AetherCore", "ReactiveRuntime", "ReactiveCore" });
    }
}
