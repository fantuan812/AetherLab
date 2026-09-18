using UnrealBuildTool;
public class AetherLabEditorTarget : TargetRules
{
    public AetherLabEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.AddRange(new[] { "AetherLab", "AetherEditor" });
    }
}
