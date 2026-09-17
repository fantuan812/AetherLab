#include "AetherContent.h"
UAetherGameContent* UAetherGameContent::Load(bool)
{
    return LoadObject<UAetherGameContent>(nullptr,TEXT("/Game/AetherCore/Data/DA_GameContent.DA_GameContent"),nullptr,LOAD_NoWarn);
}
