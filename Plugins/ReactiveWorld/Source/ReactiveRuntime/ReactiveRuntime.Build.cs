using UnrealBuildTool;
public class ReactiveRuntime : ModuleRules
{
    public ReactiveRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ReactiveCore", "GameplayTags" });
        PrivateDependencyModuleNames.AddRange(new[] { "GameplayAbilities", "GameplayTasks", "Niagara", "GeometryCollectionEngine", "FieldSystemEngine", "PhysicsCore" });
    }
}
