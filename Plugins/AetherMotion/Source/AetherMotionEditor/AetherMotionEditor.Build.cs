using UnrealBuildTool;
public class AetherMotionEditor : ModuleRules
{
    public AetherMotionEditor(ReadOnlyTargetRules Target):base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;bUseUnity=false;
        AddEngineThirdPartyPrivateStaticDependencies(Target,"OpenSSL");
        PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","AetherMotionRuntime","AnimGraph","BlueprintGraph"});
        PrivateDependencyModuleNames.AddRange(new[]{"UnrealEd","Slate","SlateCore","AssetTools","AssetRegistry","Json","AnimationCore","MeshDescription","StaticMeshDescription","SkeletalMeshDescription","IKRig","IKRigEditor"});
    }
}
