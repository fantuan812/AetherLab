#include "Modules/ModuleManager.h"
#include "Skills/AetherSkillAbilityBinding.h"
class FAetherLabModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        // 在 GAS 原生标签表冻结前，从同一规范数据注册服务器/客户端技能身份。
        FDefaultGameModuleImpl::StartupModule();
        AetherSkillBinding::RegisterDefinitions();
    }
};
IMPLEMENT_PRIMARY_GAME_MODULE(FAetherLabModule, AetherLab, "AetherLab");
