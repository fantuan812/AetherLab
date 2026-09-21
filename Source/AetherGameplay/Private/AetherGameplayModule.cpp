#include "Modules/ModuleManager.h"
#include "Skills/AetherSkillAbilityBinding.h"
class FAetherGameplayModule : public FDefaultModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultModuleImpl::StartupModule();
        // 在 GAS 标签冻结前注册同一规范数据；玩法模块可独立于装配根/玩家 UI 加载。
        AetherSkillBinding::RegisterDefinitions();
    }
};
IMPLEMENT_MODULE(FAetherGameplayModule, AetherGameplay)
