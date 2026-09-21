#include "UI/AetherUITheme.h"
const UAetherUITheme& UAetherUITheme::Get()
{
    static TWeakObjectPtr<UAetherUITheme> Theme;
    if(!Theme.IsValid())Theme=LoadObject<UAetherUITheme>(nullptr,TEXT("/Game/UI/DA_UITheme.DA_UITheme"));
    return Theme.IsValid()?*Theme.Get():*GetDefault<UAetherUITheme>();
}
