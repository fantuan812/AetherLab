using UnrealBuildTool;
public class AetherMotionEditor : ModuleRules
{
    public AetherMotionEditor(ReadOnlyTargetRules Target):base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;bUseUnity=false;
        PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","AetherMotionRuntime","AnimGraph","BlueprintGraph"});
        PrivateDependencyModuleNames.AddRange(new[]{"UnrealEd","AssetTools","AssetRegistry","Json","AnimationCore","MeshDescription","StaticMeshDescription","SkeletalMeshDescription","IKRig","IKRigEditor"});
    }
}
