#include "Combat/AetherControlledActionDefinition.h"
#include "Combat/AetherActionTiming.h"
bool FAetherControlledActionDefinition::IsValid() const
{
    const float Numbers[]={Duration,CommitTime,RecoveryTime,Cost,Cooldown,MotionSpeed,MotionTime,InvulnerabilityTime,Impulse};
    for(float V:Numbers)if(!FMath::IsFinite(V))return false;
    return !ActionId.IsNone()&&DefinitionVersion==1&&Duration>0&&Duration<=10&&CommitTime>=-1&&CommitTime<=Duration&&
        RecoveryTime>=0&&RecoveryTime<=Duration&&Cost>=0&&Cooldown>=0&&MotionSpeed>=0&&MotionTime>=0&&MotionTime<=Duration&&
        InvulnerabilityTime>=0&&InvulnerabilityTime<=Duration&&Impulse>=0&&AllowedStances>0&&AllowedStances<=15;
}
const TArray<FAetherControlledActionDefinition>& AetherControlledActions::All()
{
    using namespace AetherActionTiming;
    static const TArray<FAetherControlledActionDefinition> Rows=[]
    {
        TArray<FAetherControlledActionDefinition> R;
        const auto Add=[&](FName Id,float Duration,bool Loop=false,int32 Stances=1)->FAetherControlledActionDefinition&
        {auto& D=R.AddDefaulted_GetRef();D.ActionId=Id;D.Duration=Duration;D.bLoop=Loop;D.AllowedStances=Stances;return D;};
        for(FName Id:{FName("CrouchIdle"),FName("CrouchWalk"),FName("CrouchBack"),FName("CrouchLeft"),FName("CrouchRight")})Add(Id,1,true,2);
        Add("CarryIdle",1,true,4);Add("CarryWalk",1,true,4);Add("Guard",1,true);
        for(FName Id:{FName("DodgeForward"),FName("DodgeBack"),FName("DodgeLeft"),FName("DodgeRight")})
        {auto& D=Add(Id,DodgeDuration);D.CommitTime=0;D.RecoveryTime=DodgeDuration-DodgeMotion;D.Cost=DodgeCost;D.Cooldown=DodgeCooldown;D.MotionSpeed=DodgeSpeed;D.MotionTime=DodgeMotion;D.InvulnerabilityTime=DodgeInvulnerability;D.CancelPolicy=EAetherActionCancelPolicy::AbilityOwned;D.ContactPolicy=EAetherActionContactPolicy::Ground;}
        for(FName Id:{FName("Push"),FName("Pickup"),FName("PutDown"),FName("Throw")})
        {auto& D=Add(Id,Id=="Push"?PushDuration:Id=="Pickup"?PickupDuration:ReleaseDuration,false,(Id=="PutDown"||Id=="Throw")?4:1);D.CommitTime=Id=="Push"?PushContact:HandContact;D.RecoveryTime=D.Duration-D.CommitTime;D.Impulse=Id=="Push"?PushImpulse:0;D.ContactPolicy=EAetherActionContactPolicy::FixedObject;D.CancelPolicy=EAetherActionCancelPolicy::CancelBeforeCommitKeepCommitted;}
        Add("Rescue",1.2f,true);Add("Cast",.8f);auto& Vault=Add("Vault",VaultDuration);Vault.CommitTime=0;Vault.MotionTime=VaultDuration;Vault.ContactPolicy=EAetherActionContactPolicy::Ground;Vault.CancelPolicy=EAetherActionCancelPolicy::AbilityOwned;
        Add("Stun",1,true,7);Add("Death",1.5f,false,15);Add("GetUp",1.2f,false,8);Add("Hit",.35f,false,7);Add("Land",.18f);Add("LandHeavy",.45f);
        return R;
    }();return Rows;
}
const FAetherControlledActionDefinition* AetherControlledActions::Find(FName Id)
{return All().FindByPredicate([&](const auto& D){return D.ActionId==Id;});}
