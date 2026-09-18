using UnrealBuildTool;
public class AetherLab : ModuleRules
{
    public AetherLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false; // 分别编译技能定义/身份桥接，保留模块边界检查。
        // 仅用于旧平铺头的过渡兼容；新模块公开头按职责使用目录路径。
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] { "AetherCore", "AetherGameplay", "Core", "CoreUObject", "Engine", "InputCore", "ReactiveCore", "ReactiveRuntime", "GameplayAbilities", "GameplayTags", "GameplayTasks", "NetCore" });
        PublicDependencyModuleNames.Add("AetherEquipment");
        PrivateDependencyModuleNames.AddRange(new[] { "AIModule", "AnimGraphRuntime", "Json", "NavigationSystem", "EnhancedInput" });
    }
}
