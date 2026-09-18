#include "Modules/ModuleManager.h"
#include "Presentation/AetherPresentation.h"
#include "UI/AetherFrontierHUD.h"

namespace { UClass* CreateHUDClass() { return AAetherFrontierHUD::StaticClass(); } }
class FAetherLabModule final : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override { AetherPresentation::RegisterHUD(&CreateHUDClass); }
    virtual void ShutdownModule() override { AetherPresentation::UnregisterHUD(&CreateHUDClass); }
};
IMPLEMENT_PRIMARY_GAME_MODULE(FAetherLabModule, AetherLab, "AetherLab");
