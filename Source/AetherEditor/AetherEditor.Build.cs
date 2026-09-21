using UnrealBuildTool;

// 编辑器向运行时依赖；运行时不得反向依赖地图制作工具。
public class AetherEditor : ModuleRules
{
    public AetherEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "AetherCore", "AetherGameplay", "UnrealEd", "NavigationSystem", "Json", "AssetRegistry", "UMG", "UMGEditor", "SlateCore", "Kismet", "KismetCompiler", "AetherUI", "AetherMotionRuntime", "IKRig" });
    }
}
