#include "Abilities/AetherSpellAbility.h"
#include "AetherCombat.h"
#include "Skills/AetherSkillDefinitions.h"
#include "Skills/AetherSkillAbilityBinding.h"
UAetherSpellAbility::UAetherSpellAbility()
{ InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor; NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly; }
namespace
{
// 每次都由权威 ASC 的 Spec 解析，绝不接受客户端传入的 Rank 或效果数值。
bool ResolveSkill(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FString& Id,int32& Rank)
{
    const auto* ASC=Info?Info->AbilitySystemComponent.Get():nullptr;
    const auto* Spec=ASC?ASC->FindAbilitySpecFromHandle(H):nullptr;
    if(!Spec)return false;
    Id=AetherSkillBinding::Identify(*Spec);Rank=Spec->Level;
    return FAetherSkillDefinitionsV10::Get().Effect(Id,Rank)!=nullptr;
}
}
float UAetherSpellAbility::Cost(int32 Spell)
{
    const auto& D=FAetherSkillDefinitionsV10::Get();const auto* S=D.Legacy(Spell);
    const auto* E=S?D.Effect(S->SkillId,1):nullptr;
    return E?float(E->ManaCost):0.f;
}
bool UAetherSpellAbility::CheckCost(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayTagContainer* Tags) const
{
    FString Id;int32 Rank=0;if(!ResolveSkill(H,Info,Id,Rank))return false;
    const auto* C=Info?Cast<AAetherCharacter>(Info->AvatarActor.Get()):nullptr;
    const auto& E=*FAetherSkillDefinitionsV10::Get().Effect(Id,Rank);
    return C&&C->Ready()&&C->SkillUnlocked(Id)&&C->Mana()>=E.ManaCost
        &&C->WaterReserveKg>=E.WaterKg&&Super::CheckCost(H,Info,Tags);
}
void UAetherSpellAbility::ApplyCost(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A) const
{
    FString Id;int32 Rank=0;if(!ResolveSkill(H,Info,Id,Rank))return;
    const auto& E=*FAetherSkillDefinitionsV10::Get().Effect(Id,Rank);
    Info->AbilitySystemComponent->ApplyModToAttribute(UAetherAttributes::GetManaAttribute(),EGameplayModOp::Additive,-float(E.ManaCost));
}
void UAetherSpellAbility::ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,const FGameplayEventData* Event)
{
    FString Id;int32 Rank=0;
    auto* C=Info?Cast<AAetherCharacter>(Info->AvatarActor.Get()):nullptr;
    FHitResult Hit;FVector Origin,Direction;
    if(!C||!C->HasAuthority()||!ResolveSkill(H,Info,Id,Rank)||!C->FindSkillTarget(Id,Rank,Hit,Origin,Direction))
    {EndAbility(H,Info,A,true,true);return;}
    // 复制身份/等级/成本再 Commit，属性通知可能改变 ASC 列表，不能跨回调持有 Spec 指针。
    const float ManaCost=float(FAetherSkillDefinitionsV10::Get().Effect(Id,Rank)->ManaCost);
    if(!CommitAbility(H,Info,A)){EndAbility(H,Info,A,true,true);return;}
    const bool Executed=C->ExecuteSkill(Id,Rank);
    if(!Executed)Info->AbilitySystemComponent->ApplyModToAttribute(UAetherAttributes::GetManaAttribute(),EGameplayModOp::Additive,ManaCost);
    EndAbility(H,Info,A,true,!Executed);
}
