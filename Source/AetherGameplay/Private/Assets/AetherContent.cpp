#include "Assets/AetherContent.h"
UAetherGameContent* UAetherGameContent::Load(bool Basic)
{
    if(Basic)return FindObject<UAetherGameContent>(nullptr,TEXT("/Game/AetherCore/Data/DA_GameContent.DA_GameContent"));
    return LoadObject<UAetherGameContent>(nullptr,TEXT("/Game/AetherCore/Data/DA_GameContent.DA_GameContent"),nullptr,LOAD_NoWarn);
}
