using UnrealBuildTool;
public class AetherMotionRuntime : ModuleRules
{
    public AetherMotionRuntime(ReadOnlyTargetRules Target):base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;bUseUnity=false;
        PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","AnimGraphRuntime","IKRig"});
        PrivateDependencyModuleNames.AddRange(new[]{"Projects","Json","MotionBricksNative"});
        AddEngineThirdPartyPrivateStaticDependencies(Target,"OpenSSL");
    }
}
