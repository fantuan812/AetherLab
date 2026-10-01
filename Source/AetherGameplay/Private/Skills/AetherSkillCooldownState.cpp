#include "Skills/AetherSkillCooldownState.h"
#include "Combat/AetherCombat.h"
#include "Framework/AetherProgression.h"

double AetherSkillCooldowns::Remaining(const TArray<FAetherSkillCooldownDeadline>& Deadlines,const FString& Skill,const FString& Group,double Now)
{
    if(Skill.IsEmpty()||Group.IsEmpty()||!FMath::IsFinite(Now))return TNumericLimits<double>::Max();
    double End=Now;
    for(const auto& Deadline:Deadlines)if(Deadline.Key==TEXT("Skill.")+Skill||Deadline.Key==TEXT("Group.")+Group)
    {
        if(!FMath::IsFinite(Deadline.EndsAt))return TNumericLimits<double>::Max();
        End=FMath::Max(End,Deadline.EndsAt);
    }
    return End-Now;
}
bool AetherSkillCooldowns::Commit(TArray<FAetherSkillCooldownDeadline>& Deadlines,const FString& Skill,const FString& Group,double SkillSeconds,double GroupSeconds,double Now)
{
    if(Skill.IsEmpty()||Group.IsEmpty()||!FMath::IsFinite(Now)||!FMath::IsFinite(SkillSeconds)||!FMath::IsFinite(GroupSeconds)||
        SkillSeconds<0||GroupSeconds<0||!FMath::IsFinite(Now+SkillSeconds)||!FMath::IsFinite(Now+GroupSeconds))return false;
    for(const auto& Deadline:Deadlines)if(!FMath::IsFinite(Deadline.EndsAt))return false;
    Deadlines.RemoveAll([Now](const auto& Deadline){return Deadline.EndsAt<=Now;});
    const auto Put=[&](const FString& Key,double Seconds)
    {
        if(Seconds<=0)return;
        if(auto* Deadline=Deadlines.FindByPredicate([&](const auto& Value){return Value.Key==Key;}))Deadline->EndsAt=FMath::Max(Deadline->EndsAt,Now+Seconds);
        else {FAetherSkillCooldownDeadline Deadline;Deadline.Key=Key;Deadline.EndsAt=Now+Seconds;Deadlines.Add(MoveTemp(Deadline));}
    };
    Put(TEXT("Skill.")+Skill,SkillSeconds);Put(TEXT("Group.")+Group,GroupSeconds);return true;
}
double UAetherDefinitionAbilitySystem::CooldownRemaining(const FString& Skill,const FString& Group,double Now) const
{
    const auto* Character=Cast<AAetherCharacter>(GetOwner());
    if(!Character||!Character->HasAuthority()||Character->SkillAuthority!=EAetherSkillAuthority::Definition||
        Character->GetPlayerState<AAetherPlayerState>()||Character->AbilitySystem.Get()!=this||GetAvatarActor()!=Character)return TNumericLimits<double>::Max();
    return AetherSkillCooldowns::Remaining(SkillCooldowns,Skill,Group,Now);
}
bool UAetherDefinitionAbilitySystem::CommitCooldown(const FString& Skill,const FString& Group,double SkillSeconds,double GroupSeconds,double Now)
{
    // 调用者在效果执行前已锁定此 ASC。效果回调换 Avatar 时，已成功的动作仍记在原权威上。
    return GetOwner()&&GetOwner()->HasAuthority()&&AetherSkillCooldowns::Commit(SkillCooldowns,Skill,Group,SkillSeconds,GroupSeconds,Now);
}
