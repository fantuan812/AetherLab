#include "Modules/ModuleManager.h"
#include "UObject/CoreRedirects.h"

class FAetherEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        // 只重定向实际迁移的作者工具类，绝不把整个旧脚本包重定向到 Editor。
        // 使用 Default 阶段，避免提前触发游戏 CDO 并在动画数据模块就绪前加载资源。
        // 工具与地图在启动模块完成后加载；专服不会加载此模块。
        const TArray<FCoreRedirect> Redirects {
            FCoreRedirect(ECoreRedirectFlags::Type_Class,
                TEXT("/Script/AetherLab.AetherWorldAuthoring"),
                TEXT("/Script/AetherEditor.AetherWorldAuthoring"))
        };
        FCoreRedirects::AddRedirectList(Redirects, TEXT("AetherEditorV10"));
    }
};
IMPLEMENT_MODULE(FAetherEditorModule, AetherEditor)
