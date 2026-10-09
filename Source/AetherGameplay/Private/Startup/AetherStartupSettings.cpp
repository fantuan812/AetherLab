#include "Startup/AetherStartupSettings.h"
#include "Framework/AetherFrontendMode.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"

bool UAetherStartupSettings::DrainDeadline(double Now,double& Deadline,FString& Reason) const
{
    if(!FMath::IsFinite(Now)||!FMath::IsFinite(BackendDrainTimeoutSeconds)||BackendDrainTimeoutSeconds<=0||
        !FMath::IsFinite(Now+BackendDrainTimeoutSeconds))
    {Reason=TEXT("Startup BackendDrainTimeoutSeconds must be configured as a finite positive duration");return false;}
    Deadline=Now+BackendDrainTimeoutSeconds;Reason.Reset();return true;
}
bool UAetherStartupSettings::ResolveMap(bool Frontend,FString& Package) const
{
    Package.Reset();const auto& Map=Frontend?FrontendMap:PlayableMap;
    if(Map.IsNull())return false;
    const auto Path=Map.ToSoftObjectPath();const auto Name=Path.GetLongPackageName();
    if(!FPackageName::IsValidLongPackageName(Name)||!FPackageName::DoesPackageExist(Name))return false;
    const auto* World=Map.LoadSynchronous();if(!World)return false;
    if(Frontend)
    {
        // 包存在仍不足以保证安全停留；正式前端必须具有不装持久后端的明确模式。
        const auto* Settings=World->GetWorldSettings();
        if(!Settings||Settings->DefaultGameMode!=AAetherFrontendMode::StaticClass())return false;
    }
    Package=Name;return true;
}
