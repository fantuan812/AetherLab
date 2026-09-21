#pragma once
#include "CoreMinimal.h"

namespace AetherEquipmentMath
{
    // 所有调用点共享结算公式；数值无效拒绝为零，不允许负减免反向治疗。
    inline float PhysicalDamage(float Incoming,float Armor)
    {
        if(!FMath::IsFinite(Incoming)||!FMath::IsFinite(Armor))return 0;
        return FMath::Clamp(Incoming,0.f,100000.f)*100.f/(100.f+FMath::Clamp(Armor,0.f,900.f));
    }
    inline float ElementMultiplier(float Resistance)
    {return FMath::IsFinite(Resistance)?1.f-FMath::Clamp(Resistance,0.f,80.f)/100.f:1.f;}
    inline int32 ContinuousWear(float AppliedDamage,double& Remainder)
    {
        if(!FMath::IsFinite(AppliedDamage)||AppliedDamage<=0)return 0;
        Remainder+=FMath::Min(double(AppliedDamage),100000.);
        const int32 Count=FMath::FloorToInt(Remainder+1.e-4);
        Remainder=FMath::Max(0.,Remainder-Count);return Count;
    }
    inline float Attack(float Base,float Bonus)
    {return FMath::IsFinite(Base)&&FMath::IsFinite(Bonus)?FMath::Clamp(Base+Bonus,0.f,10000.f):0.f;}
}
