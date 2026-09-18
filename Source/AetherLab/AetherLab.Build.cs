using UnrealBuildTool;
public class AetherLab : ModuleRules
{
    public AetherLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false; // 附近对象索引、精确交互与真实场景回归分别编译，保留模块边界检查。
        // 仅用于旧平铺头的过渡兼容；新模块公开头按职责使用目录路径。
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] { "AetherCore", "AetherGameplay", "Core", "CoreUObject", "Engine", "InputCore", "ReactiveCore", "ReactiveRuntime", "GameplayAbilities", "GameplayTags", "GameplayTasks", "NetCore" });
        PublicDependencyModuleNames.Add("AetherEquipment");
        PrivateDependencyModuleNames.AddRange(new[] { "AIModule", "AnimGraphRuntime", "Json", "NavigationSystem", "EnhancedInput" });
    }
}
