#include "Framework/AetherProgression.h"
#include "Net/UnrealNetwork.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Equipment/AetherEquipmentEffect.h"
#include "Definitions/AetherV10Definitions.h"
#include "GameFramework/Pawn.h"
#include "Effects/AetherBuffRuntime.h"
#include "Skills/AetherSkillGrantResolver.h"
#include "Skills/AetherCooldownLedger.h"
#include "Engine/GameInstance.h"
AAetherPlayerState::AAetherPlayerState()
{
    AbilitySystem=CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("PersistentAbilities")); AbilitySystem->SetIsReplicated(true);
    AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    Attributes=CreateDefaultSubobject<UAetherAttributes>(TEXT("PersistentAttributes")); Attributes->Posture.SetBaseValue(100); Attributes->Posture.SetCurrentValue(100); SetNetUpdateFrequency(20);
}
void AAetherPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME_CONDITION(AAetherPlayerState,SkillCooldowns,COND_OwnerOnly); DOREPLIFETIME_CONDITION(AAetherPlayerState,Profile,COND_OwnerOnly);DOREPLIFETIME_CONDITION(AAetherPlayerState,bNativeSkillsEnabled,COND_OwnerOnly);DOREPLIFETIME_CONDITION(AAetherPlayerState,SkillGrants,COND_OwnerOnly);DOREPLIFETIME(AAetherPlayerState,DisplayName);DOREPLIFETIME(AAetherPlayerState,PartyLeader);DOREPLIFETIME(AAetherPlayerState,bPartyCaptain);DOREPLIFETIME_CONDITION(AAetherPlayerState,InvitationExpires,COND_OwnerOnly);DOREPLIFETIME_CONDITION(AAetherPlayerState,InvitedBy,COND_OwnerOnly); }
double AAetherPlayerState::CooldownRemaining(const FString& Skill,const FString& Group,double Now) const
{
    double End=Now;for(const auto& D:SkillCooldowns)if(D.Key==TEXT("Skill.")+Skill||D.Key==TEXT("Group.")+Group)End=FMath::Max(End,D.EndsAt);
    return End-Now;
}
void AAetherPlayerState::CommitCooldown(const FString& Skill,const FString& Group,double SkillSeconds,double GroupSeconds,double Now)
{
    if(!HasAuthority())return;
    SkillCooldowns.RemoveAll([Now](const auto& D){return D.EndsAt<=Now;});
    const auto Put=[&](const FString& Key,double Seconds) {
        if(Seconds<=0)return;
        if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCooldownLedger>()->Put(Profile.CharacterId,Key,Seconds);
        if(auto* D=SkillCooldowns.FindByPredicate([&](const auto& V){return V.Key==Key;}))D->EndsAt=FMath::Max(D->EndsAt,Now+Seconds);
        else {FAetherSkillCooldownDeadline Entry;Entry.Key=Key;Entry.EndsAt=Now+Seconds;SkillCooldowns.Add(MoveTemp(Entry));}
    };
    Put(TEXT("Skill.")+Skill,SkillSeconds);Put(TEXT("Group.")+Group,GroupSeconds);ForceNetUpdate();
}

bool AAetherPlayerState::PublishNativeSkills(const FAetherProfileStateV10& P,const TArray<FAetherExternalSkillGrant>& RequestedGrants,FString& Reason)
{
    check(IsInGameThread());const auto& D=FAetherV10Definitions::Get();
    auto Grants=RequestedGrants;
    Grants.RemoveAll([](const auto& G){return G.SourceId.StartsWith(TEXT("Buff."));});
    if(const auto* C=Cast<AAetherCharacter>(GetPawn()))Grants.Append(C->BuffRuntime->GetState().SkillGrants());
    if(!HasAuthority()||bPublishingNativeSkills||!D.bValid||!P.CharacterId.Equals(Profile.CharacterId,ESearchCase::CaseSensitive)||
        P.Revision<NativeSkillRevision||!P.Validate(D.Items,D.Skills,D.Rules,Reason)||!FAetherSkillStateV10::ValidateExternalGrants(Grants,D.Skills))
    {if(Reason.IsEmpty())Reason=TEXT("Invalid committed skill publication");return false;}
    if(!GetPawn()||!AbilitySystem||AbilitySystem->GetAvatarActor()!=GetPawn()){Reason=TEXT("ASC avatar not bound to current Pawn");return false;}
    TGuardValue<bool> Guard(bPublishingNativeSkills,true);bNativeSkillsEnabled=true;bNativeSkillReady=false;
    if(auto* C=Cast<AAetherCharacter>(GetPawn()))if(auto* GI=GetGameInstance())
        for(const auto& Pair:GI->GetSubsystem<UAetherCooldownLedger>()->Read(P.CharacterId))
        {
            if(auto* Existing=SkillCooldowns.FindByPredicate([&](const auto& V){return V.Key==Pair.Key;}))Existing->EndsAt=C->CombatTime()+Pair.Value;
            else {FAetherSkillCooldownDeadline Entry;Entry.Key=Pair.Key;Entry.EndsAt=C->CombatTime()+Pair.Value;SkillCooldowns.Add(MoveTemp(Entry));}
        }
    // 先记已见提交版本，失败后拒绝旧回读；同版本允许修复重试，不能退回旧等级。
    NativeSkillRevision=P.Revision;NativeSkills=P.Skills;NativeSkillGrants=Grants;
    if(!AetherSkillBinding::Publish(*AbilitySystem,P.Skills,Grants,Reason))return false;
    // 一个持续效果句柄聚合装备和被动。更换 Avatar、洗点和重复快照均替换数值，不做永久加法。
    if(!ApplyResolvedAttributes(P,Grants,Reason))return false;
    const auto Signature=AetherSkillGrants::EffectiveSignature(AetherSkillGrants::Resolve(P.Skills,D.Skills,Grants));
    if(Signature!=LastGrantSignature){LastGrantSignature=Signature;++GrantRevision;}
    SkillGrants.Rows.Reset();
    for(const auto& G:Grants){FAetherSkillGrantPresentation V;V.SourceId=G.SourceId;V.SkillId=G.SkillId;V.Rank=G.Rank;V.Source=uint8(G.Source);
        if(const auto* Temporary=TemporarySkillSources.Find(G.SourceId)){V.InstanceId=Temporary->InstanceId;V.ExpiresAtServerSeconds=Temporary->ExpiresAt;}
        SkillGrants.Rows.Add(MoveTemp(V));}
    SkillGrants.ProfileRevision=P.Revision;SkillGrants.GrantRevision=GrantRevision;++SkillGrants.Sequence;bNativeSkillReady=true;
    if(auto* C=Cast<AAetherCharacter>(GetPawn()))C->BuffRuntime->RefreshSnapshot();
    ForceNetUpdate();OnProfilePublished.Broadcast();return true;
}
bool AAetherPlayerState::RebindNativeSkills(FString& Reason)
{
    if(!HasAuthority()||!NativeProfile.IsSet()||bPublishingNativeSkills||!GetPawn()||!AbilitySystem||AbilitySystem->GetAvatarActor()!=GetPawn())
    {Reason=TEXT("Native skill avatar not ready");return false;}
    // 新身体先撤销旧生命的临时来源，再完整发布能力和持续属性，避免只有 AbilitySpec 恢复。
    RefreshTemporarySkills();
    const auto Grants=NativeSkillGrants;
    return PublishNativeSkills(NativeProfile.GetValue(),Grants,Reason);
}

TArray<FAetherExternalSkillGrant> AAetherPlayerState::GetNativeSkillGrants() const
{
    if(HasAuthority())return NativeSkillGrants;
    TArray<FAetherExternalSkillGrant> Grants;
    if(SkillGrants.Rows.Num()>256)return Grants;
    const auto* C=Cast<AAetherCharacter>(GetPawn());
    for(const auto& V:SkillGrants.Rows)
    {
        // Countdown is presentation only. Server expiry publishes the authoritative removal.
        Grants.Add({V.SourceId,V.SkillId,V.Rank,EAetherSkillGrantSource(V.Source)});
    }
    if(!FAetherSkillStateV10::ValidateExternalGrants(Grants,FAetherV10Definitions::Get().Skills))Grants.Reset();
    return Grants;
}
