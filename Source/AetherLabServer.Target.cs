using UnrealBuildTool;
public class AetherLabServerTarget : TargetRules
{
    public AetherLabServerTarget(TargetInfo Target) : base(Target)
    { Type = TargetType.Server; DefaultBuildSettings = BuildSettingsVersion.V7; IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8; ExtraModuleNames.Add("AetherLab"); }
}
