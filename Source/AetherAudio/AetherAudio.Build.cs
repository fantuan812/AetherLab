using UnrealBuildTool;
public class AetherAudio : ModuleRules
{
    public AetherAudio(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "DeveloperSettings" });
    }
}
