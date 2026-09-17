using UnrealBuildTool;
public class AetherLab : ModuleRules
{
    public AetherLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "ReactiveCore", "ReactiveRuntime", "GameplayAbilities", "GameplayTags", "GameplayTasks", "NetCore" });
        PublicDependencyModuleNames.Add("AetherEquipment");
        PrivateDependencyModuleNames.AddRange(new[] { "AIModule", "AnimGraphRuntime", "Json", "NavigationSystem", "EnhancedInput", "UMG", "Slate", "SlateCore" });
    }
}
