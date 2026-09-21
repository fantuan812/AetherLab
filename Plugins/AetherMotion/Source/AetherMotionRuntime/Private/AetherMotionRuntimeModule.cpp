#include "Modules/ModuleManager.h"
#include "MotionBricksScheduler.h"
class FAetherMotionRuntimeModule : public IModuleInterface
{
public:
    TUniquePtr<FMotionBricksScheduler> Scheduler;
    virtual bool SupportsDynamicReloading() override{return false;}
    virtual void ShutdownModule() override{Scheduler.Reset();}
};
FMotionBricksScheduler& AetherMotionScheduler()
{
    check(IsInGameThread());
    auto& M=FModuleManager::LoadModuleChecked<FAetherMotionRuntimeModule>(TEXT("AetherMotionRuntime"));
    if(!M.Scheduler)M.Scheduler=MakeUnique<FMotionBricksScheduler>();return *M.Scheduler;
}
IMPLEMENT_MODULE(FAetherMotionRuntimeModule,AetherMotionRuntime)
