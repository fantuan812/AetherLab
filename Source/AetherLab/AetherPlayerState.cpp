#include "AetherProgression.h"
#include "Net/UnrealNetwork.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Equipment/AetherEquipmentEffect.h"
#include "Definitions/AetherV10Definitions.h"
#include "GameFramework/Pawn.h"
AAetherPlayerState::AAetherPlayerState()
{
    AbilitySystem=CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("PersistentAbilities")); AbilitySystem->SetIsReplicated(true);
    AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    Attributes=CreateDefaultSubobject<UAetherAttributes>(TEXT("PersistentAttributes")); Attributes->Posture.SetBaseValue(100); Attributes->Posture.SetCurrentValue(100); SetNetUpdateFrequency(20);
}
void AAetherPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME_CONDITION(AAetherPlayerState,Profile,COND_OwnerOnly);DOREPLIFETIME_CONDITION(AAetherPlayerState,bNativeSkillsEnabled,COND_OwnerOnly);DOREPLIFETIME_CONDITION(AAetherPlayerState,SkillGrantPresentation,COND_OwnerOnly);DOREPLIFETIME_CONDITION(AAetherPlayerState,SkillGrantRevision,COND_OwnerOnly);DOREPLIFETIME(AAetherPlayerState,DisplayName);DOREPLIFETIME(AAetherPlayerState,PartyLeader);DOREPLIFETIME(AAetherPlayerState,bPartyCaptain);DOREPLIFETIME_CONDITION(AAetherPlayerState,InvitationExpires,COND_OwnerOnly);DOREPLIFETIME_CONDITION(AAetherPlayerState,InvitedBy,COND_OwnerOnly); }

bool AAetherPlayerState::PublishNativeSkills(const FAetherProfileStateV10& P,const TArray<FAetherExternalSkillGrant>& Grants,FString& Reason)
{
    check(IsInGameThread());const auto& D=FAetherV10Definitions::Get();
    if(!HasAuthority()||bPublishingNativeSkills||!D.bValid||!P.CharacterId.Equals(Profile.CharacterId,ESearchCase::CaseSensitive)||
        P.Revision<NativeSkillRevision||!P.Validate(D.Items,D.Skills,D.Rules,Reason)||!FAetherSkillStateV10::ValidateExternalGrants(Grants,D.Skills))
    {if(Reason.IsEmpty())Reason=TEXT("Invalid committed skill publication");return false;}
    if(!GetPawn()||!AbilitySystem||AbilitySystem->GetAvatarActor()!=GetPawn()){Reason=TEXT("ASC avatar not bound to current Pawn");return false;}
    TGuardValue<bool> Guard(bPublishingNativeSkills,true);bNativeSkillsEnabled=true;bNativeSkillReady=false;
    // 先记已见提交版本，失败后拒绝旧回读；同版本允许修复重试，不能退回旧等级。
    NativeSkillRevision=P.Revision;NativeSkills=P.Skills;NativeSkillGrants=Grants;
    if(!AetherSkillBinding::Publish(*AbilitySystem,P.Skills,Grants,Reason))return false;
    SkillGrantPresentation.Reset();
    for(const auto& G:Grants){FAetherSkillGrantPresentation V;V.SourceId=G.SourceId;V.SkillId=G.SkillId;V.Rank=G.Rank;V.Source=uint8(G.Source);SkillGrantPresentation.Add(MoveTemp(V));}
    SkillGrantRevision=P.Revision;bNativeSkillReady=true;ForceNetUpdate();OnProfilePublished.Broadcast();return true;
}
bool AAetherPlayerState::RebindNativeSkills(FString& Reason)
{
    if(!HasAuthority()||!NativeSkills.IsSet()||bPublishingNativeSkills||!GetPawn()||!AbilitySystem||AbilitySystem->GetAvatarActor()!=GetPawn())
    {Reason=TEXT("Native skill avatar not ready");return false;}
    TGuardValue<bool> Guard(bPublishingNativeSkills,true);bNativeSkillReady=false;
    if(!AetherSkillBinding::Publish(*AbilitySystem,NativeSkills.GetValue(),NativeSkillGrants,Reason))return false;
    bNativeSkillReady=true;return true;
}

TArray<FAetherExternalSkillGrant> AAetherPlayerState::GetNativeSkillGrants() const
{
    if(HasAuthority())return NativeSkillGrants;
    TArray<FAetherExternalSkillGrant> Grants;
    if(SkillGrantPresentation.Num()>256)return Grants;
    for(const auto& V:SkillGrantPresentation)Grants.Add({V.SourceId,V.SkillId,V.Rank,EAetherSkillGrantSource(V.Source)});
    if(!FAetherSkillStateV10::ValidateExternalGrants(Grants,FAetherV10Definitions::Get().Skills))Grants.Reset();
    return Grants;
}
