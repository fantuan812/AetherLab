using UnrealBuildTool;
public class AetherLab : ModuleRules
{
    public AetherLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "AetherCore", "AetherGameplay", "ReactiveCore", "ReactiveRuntime", "InputCore" });
    }
}
