#include "MotionBricksApi.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
bool Sha256(const FString& Path,FString& Hash)
{
    TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));if(!File)return false;
    SHA256_CTX Context;SHA256_Init(&Context);TArray<uint8> Buffer;Buffer.SetNumUninitialized(1024*1024);
    int64 Remaining=File->Size();
    while(Remaining>0){const int64 Count=FMath::Min<int64>(Remaining,Buffer.Num());if(!File->Read(Buffer.GetData(),Count))return false;SHA256_Update(&Context,Buffer.GetData(),Count);Remaining-=Count;}
    uint8 Digest[SHA256_DIGEST_LENGTH];SHA256_Final(Digest,&Context);Hash=BytesToHex(Digest,SHA256_DIGEST_LENGTH).ToLower();return true;
}
bool RelativePath(const FString& Path)
{
    if(Path.IsEmpty()||Path.Len()>160||Path.Contains(TEXT(".."))||Path.Contains(TEXT(":"))||Path.Contains(TEXT("\\"))||Path.StartsWith(TEXT("/")))return false;
    for(TCHAR C:Path)if(!FChar::IsAlnum(C)&&C!='/'&&C!='_'&&C!='-'&&C!='.')return false;
    return true;
}
}
bool FMotionBricksApi::VerifyStage(FString& Why)
{
    VerifiedFiles.Reset();StagedBackend.Reset();
    FString Text;if(!FFileHelper::LoadFileToString(Text,*(RuntimeRoot/TEXT("stage.json")))||Text.Len()>1024*1024){Why=TEXT("动作数据包尚未部署");return false;}
    TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Files=nullptr;FString Version;double Abi=0;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root||
        !Root->TryGetStringField(TEXT("nativeRevision"),Version)||Version!=Revision||!Root->TryGetNumberField(TEXT("abi"),Abi)||Abi!=1||
        !Root->TryGetStringField(TEXT("backend"),StagedBackend)||!Root->TryGetArrayField(TEXT("files"),Files)||Files->Num()<10||Files->Num()>256)
    {Why=TEXT("动作发布清单版本无效");return false;}
    if(StagedBackend!=TEXT("CPU")&&StagedBackend!=TEXT("Vulkan")){Why=TEXT("未知动作后端制品");return false;}
    for(const auto& Value:*Files)
    {
        const auto O=Value->AsObject();FString Path,Expected,Actual;double Size=0;
        if(!O||!O->TryGetStringField(TEXT("path"),Path)||!RelativePath(Path)||VerifiedFiles.Contains(Path)||
            !O->TryGetStringField(TEXT("sha256"),Expected)||Expected.Len()!=64||!O->TryGetNumberField(TEXT("bytes"),Size)||
            Size<0||Size>2.e9||Size!=FMath::FloorToDouble(Size)||FPlatformFileManager::Get().GetPlatformFile().FileSize(*(RuntimeRoot/Path))!=int64(Size)||
            !Sha256(RuntimeRoot/Path,Actual)||!Actual.Equals(Expected,ESearchCase::CaseSensitive))
        {Why=TEXT("动作数据包文件缺失或摘要不匹配：")+Path;return false;}
        VerifiedFiles.Add(Path);
    }
    for(const TCHAR* Required:{TEXT("g1-f32/manifest.json"),TEXT("g1-f32/pose.gguf"),TEXT("g1-f32/root.gguf"),TEXT("g1-f32/vq-decoder.gguf"),TEXT("g1-f32/support.gguf")})
        if(!VerifiedFiles.Contains(Required)){Why=TEXT("动作模型包不完整");return false;}
    // GGML 会枚举目录。额外 DLL 即使未显式请求，也不能绕过发布清单校验。
    TArray<FString> Libraries;
    IFileManager::Get().FindFiles(Libraries,*(RuntimeRoot/TEXT("*")),true,false);
    for(const FString& Name:Libraries)
        if((Name.EndsWith(TEXT(".dll"))||Name.Contains(TEXT(".so")))&&!VerifiedFiles.Contains(Name))
        {Why=TEXT("动作目录存在未声明的动态库：")+Name;return false;}
    return true;
}
bool FMotionBricksApi::Load(FString& Why)
{
#if UE_SERVER
    Why=TEXT("专用服务器不运行生成动作");return false;
#else
    if(Library){if(!bLoaded)Why=TEXT("原生库初始化已失败，需重启后重新验证");return bLoaded;}
    const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("AetherMotion"));if(!Plugin){Why=TEXT("动作插件未安装");return false;}
#if PLATFORM_WINDOWS
    RuntimeRoot=FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()/TEXT("Binaries/ThirdParty/Win64"));
    const FString Main=TEXT("motionbricks.dll");
#elif PLATFORM_LINUX
    RuntimeRoot=FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()/TEXT("Binaries/ThirdParty/Linux"));
    const FString Main=TEXT("libmotionbricks.so");
#else
    Why=TEXT("当前平台未配置动作原生库");return false;
#endif
#if PLATFORM_WINDOWS || PLATFORM_LINUX
    if(!VerifyStage(Why)||!VerifiedFiles.Contains(Main))return false;
    // 先加载经过摘要验证的底层依赖；OS 系统运行库仍由平台加载器处理。
    FPlatformProcess::PushDllDirectory(*RuntimeRoot);
#if PLATFORM_WINDOWS
    const TArray<FString> Prerequisites={TEXT("ggml-base.dll"),TEXT("ggml.dll")};
#else
    const TArray<FString> Prerequisites={TEXT("libggml-base.so"),TEXT("libggml.so")};
#endif
    bool OK=true;for(const auto& File:Prerequisites)
    {
        if(!VerifiedFiles.Contains(File)){OK=false;break;}
        void* Handle=FPlatformProcess::GetDllHandle(*(RuntimeRoot/File));if(!Handle){OK=false;break;}Dependencies.Add(Handle);
    }
    if(OK)Library=FPlatformProcess::GetDllHandle(*(RuntimeRoot/Main));
    FPlatformProcess::PopDllDirectory(*RuntimeRoot);
    if(!Library)
    {
        for(int32 I=Dependencies.Num()-1;I>=0;--I)FPlatformProcess::FreeDllHandle(Dependencies[I]);
        Dependencies.Reset();Why=TEXT("动作原生库或已声明依赖加载失败");return false;
    }
#define MB_FUNCTION(Name) Name=reinterpret_cast<decltype(Name)>(FPlatformProcess::GetDllExport(Library,TEXT(#Name)));if(!Name){Why=TEXT("动作库缺少必需导出：") TEXT(#Name);return false;}
#include "MotionBricksRequired.inl"
#undef MB_FUNCTION
    if(mb_abi_version()!=MB_ABI_VERSION){Why=TEXT("动作 C ABI 不兼容");return false;}
    bLoaded=true;Why.Reset();return true;
#endif
#endif
}
FMotionBricksApi::~FMotionBricksApi()
{
    // 只能由调度器在所有调用结束、模型及 agent 销毁后析构。
    if(Library)FPlatformProcess::FreeDllHandle(Library);
    for(int32 I=Dependencies.Num()-1;I>=0;--I)FPlatformProcess::FreeDllHandle(Dependencies[I]);
}
