#pragma once
#include "CoreMinimal.h"

enum class EAetherActionKind:uint8 { General, Spell, Melee, Dodge, Vault, Revive, World, Inventory, Move };
enum class EAetherActionDenial:uint8 { None, InvalidAvatar, Downed, Storage, Stunned, Silenced, Recovery, Blocking, Busy, Displacement, Carrying };
struct FAetherActionContext
{
    bool bAvatar=false,bAlive=false,bStorage=false,bStunned=false,bSilenced=false,bRecovery=false,
        bBlocking=false,bBusy=false,bDodge=false,bVault=false,bCarrying=false;
};
namespace AetherActionPolicy
{
    inline EAetherActionDenial Query(EAetherActionKind Kind,const FAetherActionContext& C,bool IgnoreOwnedDodge=false)
    {
        if(!C.bAvatar)return EAetherActionDenial::InvalidAvatar;
        if(!C.bAlive)return EAetherActionDenial::Downed;
        if(C.bStorage)return EAetherActionDenial::Storage;
        if(C.bStunned)return EAetherActionDenial::Stunned;
        if(Kind==EAetherActionKind::Spell&&C.bSilenced)return EAetherActionDenial::Silenced;
        if(Kind==EAetherActionKind::Move)return EAetherActionDenial::None;
        if(C.bRecovery)return EAetherActionDenial::Recovery;
        if(C.bBlocking)return EAetherActionDenial::Blocking;
        if(C.bBusy)return EAetherActionDenial::Busy;
        if(C.bVault||(C.bDodge&&!IgnoreOwnedDodge))return EAetherActionDenial::Displacement;
        if(C.bCarrying&&(Kind==EAetherActionKind::Spell||Kind==EAetherActionKind::Melee||Kind==EAetherActionKind::Vault))return EAetherActionDenial::Carrying;
        return EAetherActionDenial::None;
    }
    inline const TCHAR* Reason(EAetherActionDenial Code)
    {
        switch(Code) {
        case EAetherActionDenial::None:return TEXT("");
        case EAetherActionDenial::Downed:return TEXT("倒地时无法行动");
        case EAetherActionDenial::Storage:return TEXT("资源正在结算");
        case EAetherActionDenial::Stunned:return TEXT("眩晕中");
        case EAetherActionDenial::Silenced:return TEXT("沉默中，无法施法");
        case EAetherActionDenial::Recovery:return TEXT("动作恢复中");
        case EAetherActionDenial::Blocking:return TEXT("请先结束格挡");
        case EAetherActionDenial::Carrying:return TEXT("请先放下物件");
        default:return TEXT("当前动作尚未结束");
        }
    }
}
