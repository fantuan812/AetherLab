using UnrealBuildTool;
public class ReactiveCore : ModuleRules
{
    public ReactiveCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.Add("Core");
    }
}
