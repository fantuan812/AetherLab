#include "Modules/ModuleManager.h"
#include "Presentation/AetherPresentation.h"
#include "UI/AetherFrontierHUD.h"
#include "UObject/CoreRedirects.h"

namespace { UClass* CreateHUDClass() { return AAetherFrontierHUD::StaticClass(); } }
class FAetherUIModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        AetherPresentation::RegisterHUD(&CreateHUDClass);
        // 只迁移三个实际移动的类；不重定向仍属于玩法的旧脚本包。
        const TArray<FCoreRedirect> Redirects {
            FCoreRedirect(ECoreRedirectFlags::Type_Class, TEXT("/Script/AetherLab.AetherFrontierHUD"), TEXT("/Script/AetherUI.AetherFrontierHUD")),
            FCoreRedirect(ECoreRedirectFlags::Type_Class, TEXT("/Script/AetherLab.AetherFrontierPanel"), TEXT("/Script/AetherUI.AetherFrontierPanel")),
            FCoreRedirect(ECoreRedirectFlags::Type_Class, TEXT("/Script/AetherLab.AetherFrontierViewModel"), TEXT("/Script/AetherUI.AetherFrontierViewModel"))
        };
        FCoreRedirects::AddRedirectList(Redirects, TEXT("AetherUIV10"));
    }
    virtual void ShutdownModule() override { AetherPresentation::UnregisterHUD(&CreateHUDClass); }
};
IMPLEMENT_MODULE(FAetherUIModule, AetherUI);
