#include "Skills/AetherSkillCooldownState.h"
#include "Combat/AetherCombat.h"
#include "Framework/AetherProgression.h"
#include "Inventory/AetherResourceGate.h"

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

namespace
{
bool OwnsDefinitionActor(const UAetherDefinitionAbilitySystem& System,const AAetherCharacter* C)
{
    return IsValid(C)&&!C->IsActorBeingDestroyed()&&C->HasAuthority()&&C->SkillAuthority==EAetherSkillAuthority::Definition&&
        !C->GetPlayerState<AAetherPlayerState>()&&C->AbilitySystem.Get()==&System&&System.GetOwner()==C;
}
}
void UAetherDefinitionAbilitySystem::SynchronizeDefinitionLife()
{
    if(bLifeStopped)return;
    auto* C=Cast<AAetherCharacter>(GetAvatarActor());
    // UnPossessed的ClearActorInfo是控制空窗，不是同一活体死亡；资格查询在空窗仍失败关闭。
    if(!GetAvatarActor())return;
    if(!OwnsDefinitionActor(*this,C)||GetOwnerActor()!=C){LifeId.Invalidate();LifeAvatar.Reset();return;}
    if(LifeAvatar.Get()!=C){LifeId.Invalidate();LifeAvatar=C;}
    if(!HealthChangedHandle.IsValid())HealthChangedHandle=GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).AddUObject(this,&UAetherDefinitionAbilitySystem::OnDefinitionHealthChanged);
    if(!FMath::IsFinite(C->Health())||C->Health()<=0){LifeId.Invalidate();return;}
    if(!LifeId.IsValid())LifeId=FGuid::NewGuid();
}
void UAetherDefinitionAbilitySystem::OnDefinitionHealthChanged(const FOnAttributeChangeData& Change)
{
    // 监听真实属性边沿，不能靠低频Tick漏掉同帧死亡/复活；不触碰技能冷却表。
    if(!FMath::IsFinite(Change.NewValue)||Change.NewValue<=0)LifeId.Invalidate();
    SynchronizeDefinitionLife();
}
void UAetherDefinitionAbilitySystem::InitAbilityActorInfo(AActor* OwnerActor,AActor* AvatarActor)
{
    // 先记录真实非空Avatar/权威边沿；Super回调即使往返原体，也不能复活旧生命。
    // nullptr Avatar且同一Owner仅是控制空窗，不伪造新生命。
    if((AvatarActor&&AvatarActor!=LifeAvatar.Get())||(OwnerActor&&OwnerActor!=GetOwner())||(AvatarActor&&OwnerActor!=GetOwner()))
    {LifeId.Invalidate();LifeAvatar.Reset();}
    Super::InitAbilityActorInfo(OwnerActor,AvatarActor);
    // Super可重入绑定，始终检查返回后的当前ActorInfo，不能按旧实参重建生命。
    SynchronizeDefinitionLife();
}
void UAetherDefinitionAbilitySystem::BeginPlay()
{Super::BeginPlay();bLifeStopped=false;SynchronizeDefinitionLife();}
void UAetherDefinitionAbilitySystem::EndPlay(const EEndPlayReason::Type Reason)
{
    bLifeStopped=true;LifeId.Invalidate();LifeAvatar.Reset();
    if(HealthChangedHandle.IsValid())GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).Remove(HealthChangedHandle);
    HealthChangedHandle.Reset();Super::EndPlay(Reason);
}
FGuid UAetherDefinitionAbilitySystem::DefinitionLife() const
{
    const auto* C=LifeAvatar.Get();
    return !bLifeStopped&&OwnsDefinitionActor(*this,C)&&C->Alive()&&GetOwnerActor()==C&&GetAvatarActor()==C?LifeId:FGuid();
}
bool AetherSkillLives::Resolve(const AAetherCharacter& C,FGuid& Out)
{
    Out.Invalidate();const auto* ASC=C.AbilitySystem.Get();
    if(!C.HasAuthority()||C.IsActorBeingDestroyed()||!ASC||ASC->GetAvatarActor()!=&C)return false;
    if(C.SkillAuthority==EAetherSkillAuthority::Profile)
    {
        const auto* PS=C.GetPlayerState<AAetherPlayerState>();const auto* Receiver=C.ResourceGate?C.ResourceGate->GetReceiver():nullptr;
        if(!PS||PS->AbilitySystem!=ASC||ASC->GetOwnerActor()!=PS||!Receiver)return false;
        Out=Receiver->State().LifeId;
    }
    else if(C.SkillAuthority==EAetherSkillAuthority::Definition)
    {
        const auto* Definition=Cast<UAetherDefinitionAbilitySystem>(ASC);
        if(!Definition||C.GetPlayerState<AAetherPlayerState>()||ASC->GetOwnerActor()!=&C)return false;
        Out=Definition->DefinitionLife();
    }
    return Out.IsValid();
}
