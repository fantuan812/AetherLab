#include "Presentation/AetherPresentation.h"
namespace { AetherPresentation::FHUDFactory HUDFactory = nullptr; }

void AetherPresentation::RegisterHUD(FHUDFactory Factory)
{
    check(IsInGameThread());
    check(Factory && (!HUDFactory || HUDFactory == Factory));
    HUDFactory = Factory;
}
void AetherPresentation::UnregisterHUD(FHUDFactory Factory)
{
    check(IsInGameThread());
    if (HUDFactory == Factory) HUDFactory = nullptr;
}
UClass* AetherPresentation::ResolveHUD()
{
    check(IsInGameThread());
    return HUDFactory ? HUDFactory() : nullptr;
}
