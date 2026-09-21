#pragma once
#include "Animation/AnimInstance.h"
#include "AetherMotionSourceAnimInstance.generated.h"
UCLASS(Transient)
class AETHERMOTIONRUNTIME_API UAetherMotionSourceAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
