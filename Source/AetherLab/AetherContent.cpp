#include "AetherContent.h"
UAetherGameContent* UAetherGameContent::Load()
{
    return LoadObject<UAetherGameContent>(nullptr,TEXT("/Game/SwordMagic/Data/DA_GameContent.DA_GameContent"),nullptr,LOAD_NoWarn);
}
