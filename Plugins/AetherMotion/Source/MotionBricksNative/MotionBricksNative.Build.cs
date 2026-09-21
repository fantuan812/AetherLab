using UnrealBuildTool;
using System.IO;
using EpicGames.Core;
public class MotionBricksNative : ModuleRules
{
    public MotionBricksNative(ReadOnlyTargetRules Target) : base(Target)
    {
        Type=ModuleType.External;
        PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory,"include"));
        if(Target.Type==TargetType.Server)return; // 专服无模型、DLL 或推理后端。
        string Platform=Target.Platform.ToString();
        string Runtime=Path.GetFullPath(Path.Combine(ModuleDirectory,"../../Binaries/ThirdParty",Platform));
        string Manifest=Path.Combine(Runtime,"stage.json");
        if(!File.Exists(Manifest))
        {
            if(Target.Configuration==UnrealTargetConfiguration.Shipping)
                throw new BuildException("Motion bundle is missing; run StageMotionRuntime.ps1 before a Shipping build.");
            return; // 编辑器仍能编译作者工具；运行时明确报告资源缺失。
        }
        var Doc=JsonObject.Read(new FileReference(Manifest));
        if(Doc.GetStringField("nativeRevision")!="ee0cf5d9035f639ed0787f390fb1ce05d6a4c463")
            throw new BuildException("Motion native revision differs from the lock.");
        RuntimeDependencies.Add(Manifest,StagedFileType.NonUFS);
        foreach(var Entry in Doc.GetObjectArrayField("files"))
        {
            string Relative=Entry.GetStringField("path");
            string PathName=Path.GetFullPath(Path.Combine(Runtime,Relative));
            if(!PathName.StartsWith(Runtime+Path.DirectorySeparatorChar,System.StringComparison.OrdinalIgnoreCase)||!File.Exists(PathName))
                throw new BuildException("Invalid/missing Motion stage entry: "+Relative);
            RuntimeDependencies.Add(PathName,StagedFileType.NonUFS);
        }
    }
}
