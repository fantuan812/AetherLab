#include "AetherProgression.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Definitions/AetherV10Definitions.h"
#include "TimerManager.h"
#include "Engine/World.h"

bool AAetherPlayerState::GrantRestBlessing(FString& Reason)
{
    auto* C=Cast<AAetherFrontierCharacter>(GetPawn());
    if(!HasAuthority()||!C||!C->Alive()||!NativeProfile.IsSet()||!bNativeSkillReady)
    {Reason=TEXT("角色技能尚未就绪");return false;}
    // 只增强已经获得的引泉，不允许休息跳过故事必需训练。
    if(NativeProfile->Skills.PermanentRank(TEXT("Water.Draw"))<=0){Reason.Reset();return true;}
    RefreshTemporarySkills();
    auto Grants=NativeSkillGrants;
    const double End=C->CombatTime()+300;
    const TPair<const TCHAR*,const TCHAR*> Gifts[]={{TEXT("Inn.WaterTraining"),TEXT("Water.Draw")},{TEXT("Inn.WaterWard"),TEXT("Water.Ward")}};
    for(const auto& Gift:Gifts)
    {
        Grants.RemoveAll([&](const auto& G){return G.Source==EAetherSkillGrantSource::Temporary&&G.SourceId==Gift.Key;});
        Grants.Add({Gift.Key,Gift.Value,Gift.Value==FString(TEXT("Water.Draw"))?2:1,EAetherSkillGrantSource::Temporary});
        // 刷新持续时间沿用同一实例；旧详情不会意外指向另一种效果。
        auto& T=TemporarySkillSources.FindOrAdd(Gift.Key);
        if(!T.InstanceId.IsValid())T.InstanceId=FGuid::NewGuid();T.ExpiresAt=End;
    }
    TemporaryGrantAvatar=C;
    GetWorld()->GetTimerManager().SetTimer(TemporaryGrantTimer,this,&AAetherPlayerState::RefreshTemporarySkills,.25f,true);
    if(!PublishNativeSkills(NativeProfile.GetValue(),Grants,Reason))return false;
    return true;
}
void AAetherPlayerState::RefreshTemporarySkills()
{
    if(!HasAuthority()||bPublishingNativeSkills)return;
    auto* C=Cast<AAetherFrontierCharacter>(GetPawn());
    const bool SameLife=C&&C==TemporaryGrantAvatar.Get()&&C->Alive();
    const double Now=C?C->CombatTime():0;
    TSet<FString> Remove;
    for(const auto& Pair:TemporarySkillSources)if(!SameLife||Pair.Value.ExpiresAt<=Now)Remove.Add(Pair.Key);
    if(Remove.IsEmpty())return;
    for(const auto& Key:Remove)TemporarySkillSources.Remove(Key);
    NativeSkillGrants.RemoveAll([&](const auto& G){return G.Source==EAetherSkillGrantSource::Temporary&&Remove.Contains(G.SourceId);});
    if(TemporarySkillSources.IsEmpty()){GetWorld()->GetTimerManager().ClearTimer(TemporaryGrantTimer);TemporaryGrantAvatar.Reset();}
    // 授权撤销与详情复制同时发布；持久学习/快捷位仍保留，过期不能删除永久技能。
    if(NativeProfile.IsSet()&&AbilitySystem&&AbilitySystem->GetAvatarActor()==GetPawn())
    {
        FString Why;
        const auto Grants=NativeSkillGrants;
        if(!PublishNativeSkills(NativeProfile.GetValue(),Grants,Why))
            UE_LOG(LogTemp,Error,TEXT("AETHER_TEMPORARY_SKILL_REVOKE_FAILED %s"),*Why);
    }
}
void AAetherPlayerState::EndPlay(const EEndPlayReason::Type Reason)
{
    if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(TemporaryGrantTimer);
    TemporarySkillSources.Reset();TemporaryGrantAvatar.Reset();Super::EndPlay(Reason);
}
