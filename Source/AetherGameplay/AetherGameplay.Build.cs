using UnrealBuildTool;
public class AetherGameplay : ModuleRules
{
    public AetherGameplay(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "AetherCore", "AetherEquipment", "InputCore", "ReactiveCore", "ReactiveRuntime", "GameplayAbilities", "GameplayTags", "GameplayTasks", "NetCore", "EnhancedInput" });
        PrivateDependencyModuleNames.AddRange(new[] { "ApplicationCore", "CoreOnline", "AIModule", "AnimGraphRuntime", "Json", "NavigationSystem", "AetherMotionRuntime", "IKRig" });
        if(Target.Type!=TargetType.Server)PrivateDependencyModuleNames.Add("Slate"); // 仅客户端诊断采样 Slate；专服不依赖 UI。
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        // SQLite 的单写线程、WAL 与版本锁属于 Gameplay 持久服务，不泄露给 Widget。
        PrivateDefinitions.AddRange(new[] { "SQLITE_THREADSAFE=1", "SQLITE_OMIT_LOAD_EXTENSION=1", "SQLITE_DQS=0", "SQLITE_ENABLE_API_ARMOR=1", "SQLITE_HAVE_ISNAN=1" });
    }
}
