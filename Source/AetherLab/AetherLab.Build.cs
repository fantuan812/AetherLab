using UnrealBuildTool;
public class AetherLab : ModuleRules
{
    public AetherLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false; // GAS 闪避、移动预测与胶囊碰撞回归分别编译，保留模块边界检查。
        // 仅用于旧平铺头的过渡兼容；新模块公开头按职责使用目录路径。
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] { "AetherCore", "AetherGameplay", "Core", "CoreUObject", "Engine", "InputCore", "ReactiveCore", "ReactiveRuntime", "GameplayAbilities", "GameplayTags", "GameplayTasks", "NetCore" });
        PublicDependencyModuleNames.Add("AetherEquipment");
        // 生产旧档迁移复用引擎固定 OpenSSL 的 SHA256；Windows GenericPlatform 实现不能用于该摘要。
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        PrivateDependencyModuleNames.AddRange(new[] { "AIModule", "AnimGraphRuntime", "Json", "NavigationSystem", "EnhancedInput", "AetherMotionRuntime", "IKRig" });
    }
}
