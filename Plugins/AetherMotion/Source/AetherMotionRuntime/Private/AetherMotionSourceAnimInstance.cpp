#include "AetherMotionSourceAnimInstance.h"
#include "AnimNode_AetherGeneratedPose.h"
#include "Animation/AnimInstanceProxy.h"
struct FAetherSourceProxy : FAnimInstanceProxy
{
    FAnimNode_AetherGeneratedPose Generated;
    explicit FAetherSourceProxy(UAnimInstance* Owner):FAnimInstanceProxy(Owner){}
    virtual FAnimNode_Base* GetCustomRootNode() override{return &Generated;}
    virtual void PreUpdate(UAnimInstance* Owner,float Dt) override{FAnimInstanceProxy::PreUpdate(Owner,Dt);Generated.PreUpdate(Owner);}
};
FAnimInstanceProxy* UAetherMotionSourceAnimInstance::CreateAnimInstanceProxy(){return new FAetherSourceProxy(this);}
void UAetherMotionSourceAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* P){delete P;}
