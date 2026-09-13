using UnrealBuildTool;
public class AetherLab : ModuleRules
{
    public AetherLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "ReactiveCore", "ReactiveRuntime", "GameplayAbilities", "GameplayTags", "GameplayTasks", "NetCore" });
        PublicDependencyModuleNames.Add("AetherEquipment");
        PrivateDependencyModuleNames.Add("AIModule");
    }
}
