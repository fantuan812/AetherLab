using UnrealBuildTool;

// AnimGraph 编辑节点必须在未 Cook 的 -game 运行中也可加载，防止蓝图加载时丢失输出。
// UncookedOnly 在 Shipping Cook 后完全剥离，不把编辑器依赖带入运行时。
public class AetherAnimationEditor : ModuleRules
{
 public AetherAnimationEditor(ReadOnlyTargetRules Target) : base(Target)
 {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","AetherGameplay","AnimGraph","BlueprintGraph"});
 }
}
