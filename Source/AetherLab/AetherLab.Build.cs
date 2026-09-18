using UnrealBuildTool;
public class AetherLab : ModuleRules
{
    public AetherLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        // 仅用于旧平铺头的过渡兼容；新模块公开头按职责使用目录路径。
        PublicIncludePaths.Add(ModuleDirectory);
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
        PublicDependencyModuleNames.AddRange(new[] { "AetherCore", "Core", "CoreUObject", "Engine", "InputCore", "ReactiveCore", "ReactiveRuntime", "GameplayAbilities", "GameplayTags", "GameplayTasks", "NetCore" });
        PublicDependencyModuleNames.Add("AetherEquipment");
        PrivateDependencyModuleNames.AddRange(new[] { "AIModule", "AnimGraphRuntime", "Json", "NavigationSystem", "EnhancedInput", "UMG", "Slate", "SlateCore" });
    }
}
