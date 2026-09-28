#include "Framework/AetherProgression.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Definitions/AetherV10Definitions.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Inventory/AetherResourceGate.h"
#include "Effects/AetherBuffRuntime.h"

bool AAetherPlayerState::GrantRestBlessing(FString& Reason)
{
    auto* C=Cast<AAetherFrontierCharacter>(GetPawn());
    if(!HasAuthority()||!C||!C->Alive()||!NativeProfile.IsSet()||!bNativeSkillReady)
    {Reason=TEXT("角色技能尚未就绪");return false;}
    // 只增强已经获得的引泉，不允许休息跳过故事必需训练。
    if(NativeProfile->Skills.PermanentRank(TEXT("Water.Draw"))<=0){Reason.Reset();return true;}
    return C->BuffRuntime->ApplyRestBlessing(Reason);
}
void AAetherPlayerState::RefreshTemporarySkills()
{
    if(!HasAuthority()||bPublishingNativeSkills)return;
    auto* C=Cast<AAetherFrontierCharacter>(GetPawn());
    const bool SameLife=C&&C==TemporaryGrantAvatar.Get()&&C->Alive();
    const double Now=C?C->CombatTime():0;
    TSet<FString> Remove;
    for(const auto& Pair:TemporarySkillSources)if(!SameLife||Pair.Value.ExpiresAt<=Now)Remove.Add(Pair.Key);
    if(C&&C->ResourceGate->IsBlocked()&&(!Remove.IsEmpty()||bTemporaryPublicationPending))
    {
        if(!bTemporaryExpiryQueued)
        {
            bTemporaryExpiryQueued=true;const TWeakObjectPtr<AAetherPlayerState> Self=this;
            C->ResourceGate->Defer([Self] {
                if(!Self.IsValid())return;Self->bTemporaryExpiryQueued=false;Self->RefreshTemporarySkills();
            });
        }
        return;
    }
    if(Remove.IsEmpty()&&!bTemporaryPublicationPending)
    {
        if(TemporarySkillSources.IsEmpty()){GetWorld()->GetTimerManager().ClearTimer(TemporaryGrantTimer);TemporaryGrantAvatar.Reset();}
        return;
    }
    for(const auto& Key:Remove)TemporarySkillSources.Remove(Key);
    NativeSkillGrants.RemoveAll([&](const auto& G){return G.Source==EAetherSkillGrantSource::Temporary&&Remove.Contains(G.SourceId);});
    bTemporaryPublicationPending=true;
    // 授权撤销与详情复制同时发布；持久学习/快捷位仍保留，过期不能删除永久技能。
    if(NativeProfile.IsSet()&&AbilitySystem&&AbilitySystem->GetAvatarActor()==GetPawn())
    {
        FString Why;
        const auto Grants=NativeSkillGrants;
        if(PublishNativeSkills(NativeProfile.GetValue(),Grants,Why))
        {
            bTemporaryPublicationPending=false;
            if(TemporarySkillSources.IsEmpty()){GetWorld()->GetTimerManager().ClearTimer(TemporaryGrantTimer);TemporaryGrantAvatar.Reset();}
        }
        else
        {
            bNativeSkillReady=false;AbilitySystem->CancelAllAbilities();
            UE_LOG(LogTemp,Warning,TEXT("AETHER_TEMPORARY_SKILL_REVOKE_RETRY %s"),*Why);
        }
    }
}
void AAetherPlayerState::EndPlay(const EEndPlayReason::Type Reason)
{
    if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(TemporaryGrantTimer);
    TemporarySkillSources.Reset();TemporaryGrantAvatar.Reset();Super::EndPlay(Reason);
}
