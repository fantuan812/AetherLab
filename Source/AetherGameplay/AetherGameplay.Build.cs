using UnrealBuildTool;
public class AetherGameplay : ModuleRules
{
    public AetherGameplay(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false; // 单写事务、版本索引、容器命令和经济事务分别编译验证。
        PublicDependencyModuleNames.AddRange(new[] { "Core", "AetherCore" });
        // 自带固定 SQLite 版本，使用官方 OS 锁与 WAL，不链接引擎旧 SQLiteCore。
        PrivateDefinitions.AddRange(new[] { "SQLITE_THREADSAFE=1", "SQLITE_OMIT_LOAD_EXTENSION=1", "SQLITE_DQS=0", "SQLITE_ENABLE_API_ARMOR=1", "SQLITE_HAVE_ISNAN=1" });
    }
}
