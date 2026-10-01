#include "Effects/AetherBuffRuntime.h"
#include "Combat/AetherCombat.h"
#include "Framework/AetherProgression.h"
#include "Inventory/AetherResourceGate.h"
#include "Engine/DamageEvents.h"
#include "Net/UnrealNetwork.h"
#include "Misc/DateTime.h"
#include "NativeGameplayTags.h"
#include "Abilities/AetherSpellAbility.h"
#include "Skills/AetherSkillCooldownState.h"
#include "Definitions/AetherV10Definitions.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AetherBuffSilence,"State.Aether.Buff.Silenced");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AetherBuffStun,"State.Aether.Buff.Stunned");
namespace
{
bool DefinitionEffectSupported(const FAetherBuffDefinition& Definition,FString* Why)
{
    if(Definition.Period<=0||Definition.Operations.IsEmpty())
    {if(Why)*Why=TEXT("Definition效果当前要求明确的周期治疗操作。");return false;}
    for(const auto& O:Definition.Operations)
        if(O.Kind!=EAetherBuffOperation::Heal||!O.Id.Equals(TEXT("Health"),ESearchCase::CaseSensitive))
        {if(Why)*Why=FString::Printf(TEXT("Definition效果不支持操作%d/%s；当前仅支持周期Health治疗。"),int32(O.Kind),*O.Id);return false;}
    return true;
}
bool CanProject(const AAetherCharacter& C,const FAetherBuffState& Candidate,FString* Why=nullptr)
{
    FGuid Life;if(!AetherSkillLives::Resolve(C,Life)||Candidate.LifeId!=Life)
    {if(Why)*Why=TEXT("效果生命或技能权威尚未就绪。");return false;}
    if(C.SkillAuthority==EAetherSkillAuthority::Definition)
    {
        for(const auto& I:Candidate.Instances)if(!DefinitionEffectSupported(I.Definition,Why))return false;
        return true;
    }
    if(C.SkillAuthority!=EAetherSkillAuthority::Profile)return false;
    const auto* PS=C.GetPlayerState<AAetherPlayerState>();const auto* P=PS?PS->GetNativeProfile():nullptr;
    if(!P){if(Why)*Why=TEXT("Profile效果需要当前native档案，不使用Definition降级路径。");return false;}
    auto Grants=PS->GetNativeSkillGrants();Grants.RemoveAll([](const auto& G){return G.SourceId.StartsWith(TEXT("Buff."));});Grants.Append(Candidate.SkillGrants());
    const auto& D=FAetherV10Definitions::Get();FAetherResolvedAttributes Out;FString Error;
    const bool Valid=D.bValid&&FAetherSkillStateV10::ValidateExternalGrants(Grants,D.Skills)&&AetherAttributes::ResolveProfile(*P,D.Items,D.Skills,Grants,Candidate.Attributes(),Out,Error);
    if(!Valid&&Why)*Why=TEXT("效果超出当前Profile属性或技能来源范围。");return Valid;
}
}

UAetherBuffRuntime::UAetherBuffRuntime()
{SetIsReplicatedByDefault(true);PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.1f;}
bool FAetherEffectPresentationSnapshot::NetSerialize(FArchive& Ar,UPackageMap*,bool& Success)
{
    Ar<<LifeId<<ProfileRevision<<EffectRevision<<ProjectionRevision<<GrantRevision<<bSilenced<<bStunned<<bReady;
    if(Ar.IsSaving()&&Rows.Num()>64){Success=false;return false;}
    uint8 Count=uint8(Rows.Num());Ar<<Count;if(Count>64){Success=false;return false;}
    if(Ar.IsLoading())Rows.SetNum(Count);
    for(auto& Row:Rows)
    {
        Ar<<Row.InstanceId<<Row.BuffId<<Row.Source<<Row.Stacks<<Row.ExpiresAt<<Row.bSuppressed;
        if(!Row.InstanceId.IsValid()||Row.BuffId.IsEmpty()||Row.BuffId.Len()>96||Row.Source.Len()>4128||Row.Stacks<1||Row.Stacks>32||!FMath::IsFinite(Row.ExpiresAt)||Row.ExpiresAt<0)
        {Success=false;return false;}
    }
    Ar<<Health<<Mana<<Stamina<<MaxHealth<<MaxMana<<MaxStamina<<SpellDenial;
    const auto Map=[&](TMap<FString,double>& Values,int32 Limit) {
        uint16 N=uint16(Values.Num());if(Ar.IsSaving()&&Values.Num()>Limit)return false;Ar<<N;if(N>Limit)return false;
        if(Ar.IsLoading())Values.Reset();
        if(Ar.IsSaving()){for(auto& P:Values){FString Key=P.Key;double Value=P.Value;Ar<<Key<<Value;}}
        else for(int32 Index=0;Index<N;++Index){FString Key;double Value=0;Ar<<Key<<Value;if(Key.IsEmpty()||Key.Len()>128||!FMath::IsFinite(Value)||Values.Contains(Key))return false;Values.Add(Key,Value);}
        return !Ar.IsError();
    };
    if(!Map(Attributes,12)||!Map(Cooldowns,256)){Success=false;return false;}
    uint16 ContributionsCount=uint16(Contributions.Num());Ar<<ContributionsCount;
    if(ContributionsCount>1024){Success=false;return false;}
    if(Ar.IsLoading())Contributions.SetNum(ContributionsCount);
    for(auto& P:Contributions){Ar<<P.AttributeId<<P.SourceId<<P.Value<<P.Operation;if(P.AttributeId.Len()>96||P.SourceId.Len()>128||!FMath::IsFinite(P.Value)||P.Operation>3){Success=false;return false;}}
    Success=!Ar.IsError()&&FMath::IsFinite(Health)&&FMath::IsFinite(Mana)&&FMath::IsFinite(Stamina)&&FMath::IsFinite(MaxHealth)&&FMath::IsFinite(MaxMana)&&FMath::IsFinite(MaxStamina);
    return Success;
}
bool UAetherBuffRuntime::PresentationReady(int64 Revision) const
{
    const auto* C=Cast<AAetherCharacter>(GetOwner());const auto* PS=C?C->GetPlayerState<AAetherPlayerState>():nullptr;
    return PS&&Snapshot.bReady&&Snapshot.LifeId.IsValid()&&Snapshot.ProfileRevision==Revision&&PS->SkillGrants.ProfileRevision==Revision&&PS->SkillGrants.GrantRevision==Snapshot.GrantRevision;
}
float UAetherBuffRuntime::MovementSpeedAt(double Time) const
{
    float Speed=SpeedHistory.IsEmpty()?MoveSpeedMultiplier:SpeedHistory[0].Speed;
    for(const auto& Boundary:SpeedHistory){if(Boundary.Time>Time)break;Speed=Boundary.Speed;}
    return Speed;
}
void UAetherBuffRuntime::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UAetherBuffRuntime,Snapshot,COND_OwnerOnly);
    DOREPLIFETIME(UAetherBuffRuntime,MovementConfig);
}
void UAetherBuffRuntime::OnRep_Movement()
{MoveSpeedMultiplier=MovementConfig.Speed;ActionSpeedMultiplier=MovementConfig.ActionSpeed;MovementConfigRevision=MovementConfig.Revision;MovementConfigTime=MovementConfig.Time;}
bool UAetherBuffRuntime::HasTag(const FString& Tag) const
{
    if(GetOwner()->HasAuthority())
    {const auto* C=Cast<AAetherCharacter>(GetOwner());FGuid Life;return C&&AetherSkillLives::Resolve(*C,Life)&&State.LifeId==Life&&State.HasTag(Tag);}
    return (Tag==TEXT("Silence")&&Snapshot.bSilenced)||(Tag==TEXT("Stun")&&Snapshot.bStunned);
}
bool UAetherBuffRuntime::Publish()
{
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C||!C->HasAuthority()||bPublishing)return false;
    TGuardValue<bool> Guard(bPublishing,true);
    if(C->SkillAuthority==EAetherSkillAuthority::Definition)
    {
        if(!CanProject(*C,State)){bPublicationPending=true;return false;}
        bPublicationPending=false;PublishTags();return true; // 不为NPC伪造Profile快照/属性投影。
    }
    if(C->SkillAuthority!=EAetherSkillAuthority::Profile)return false;
    auto* PS=C->GetPlayerState<AAetherPlayerState>();FString Why;
    if(!PS||!PS->RebindNativeSkills(Why)){bPublicationPending=true;return false;}
    bPublicationPending=false;PublishTags();RefreshSnapshot();return true;
}
void UAetherBuffRuntime::PublishTags()
{
    auto* C=Cast<AAetherCharacter>(GetOwner());auto* ASC=C?C->AbilitySystem.Get():nullptr;
    if(TagOwner.Get()!=ASC)
    {
        if(TagOwner.IsValid()){if(bOwnedSilence)TagOwner->RemoveLooseGameplayTag(TAG_AetherBuffSilence);if(bOwnedStun)TagOwner->RemoveLooseGameplayTag(TAG_AetherBuffStun);}
        bOwnedSilence=bOwnedStun=false;TagOwner=ASC;
    }
    if(!ASC)return;
    const auto Update=[ASC](FGameplayTag Tag,bool Desired,bool& Owned){if(Desired==Owned)return;if(Desired)ASC->AddLooseGameplayTag(Tag);else ASC->RemoveLooseGameplayTag(Tag);Owned=Desired;};
    Update(TAG_AetherBuffSilence,State.HasTag(TEXT("Silence")),bOwnedSilence);
    Update(TAG_AetherBuffStun,State.HasTag(TEXT("Stun")),bOwnedStun);
    if(bOwnedStun)C->CancelActions();
    else if(bOwnedSilence)
    {
        TArray<FGameplayAbilitySpecHandle> Cancel;
        for(const auto& Spec:ASC->GetActivatableAbilities())if(Spec.Ability&&Spec.Ability->IsA<UAetherSpellAbility>()&&Spec.IsActive())Cancel.Add(Spec.Handle);
        for(const auto Handle:Cancel)ASC->CancelAbilityHandle(Handle);
    }
}
void UAetherBuffRuntime::EndPlay(const EEndPlayReason::Type Reason)
{
    if(TagOwner.IsValid()){if(bOwnedSilence)TagOwner->RemoveLooseGameplayTag(TAG_AetherBuffSilence);if(bOwnedStun)TagOwner->RemoveLooseGameplayTag(TAG_AetherBuffStun);}
    TagOwner.Reset();bOwnedSilence=bOwnedStun=false;State={};Super::EndPlay(Reason);
}
void UAetherBuffRuntime::RefreshSnapshot()
{
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C||!C->HasAuthority()||bPublicationPending)return;
    const auto* PS=C->GetPlayerState<AAetherPlayerState>();if(!PS||!PS->GetNativeProfile()||!PS->GetNativeSkills())return;
    FAetherEffectPresentationSnapshot Next;Next.LifeId=State.LifeId;Next.EffectRevision=State.Revision;
    Next.ProjectionRevision=PS->ProjectionRevision;
    Next.GrantRevision=PS->GrantRevision;
    if(const auto* P=PS->GetNativeProfile())Next.ProfileRevision=P->Revision;
    Next.bSilenced=State.HasTag(TEXT("Silence"));Next.bStunned=State.HasTag(TEXT("Stun"));
    Next.bReady=C->ResourceGate->IsPresentationSettled()&&!HasDue();
    Next.Health=C->Health();Next.Mana=C->Mana();Next.Stamina=C->Stamina();
    Next.MaxHealth=C->MaxHealth;Next.MaxMana=C->MaximumMana();Next.MaxStamina=C->MaximumStamina();
    Next.Attributes=PS->ResolvedAttributes.Values;Next.SpellDenial=uint8(C->QueryAction(EAetherActionKind::Spell));
    Next.Attributes.Add(TEXT("MaxHealth"),Next.MaxHealth);Next.Attributes.Add(TEXT("MaxMana"),Next.MaxMana);Next.Attributes.Add(TEXT("MaxStamina"),Next.MaxStamina);
    for(const auto& D:PS->SkillCooldowns)Next.Cooldowns.Add(D.Key,D.EndsAt);
    for(const auto& A:PS->ResolvedAttributes.Contributions)
    {FAetherAttributePresentation P;P.AttributeId=A.AttributeId;P.SourceId=A.SourceId;P.Value=A.Value;P.Operation=uint8(A.Operation);Next.Contributions.Add(MoveTemp(P));}
    for(const auto& I:State.Instances)
    {
        FAetherBuffPresentation Row;Row.InstanceId=I.InstanceId;Row.BuffId=I.Definition.Id;Row.Stacks=I.Stacks();
        Row.ExpiresAt=I.ExpiresAt;Row.bSuppressed=I.bSuppressed;
        TArray<FString> Sources;I.Sources.GetKeys(Sources);Sources.Sort();Row.Source=FString::Join(Sources,TEXT(", "));Next.Rows.Add(MoveTemp(Row));
    }
    const float Speed=float(PS->ResolvedAttributes.Values.FindRef(TEXT("MoveSpeed")));
    if(SpeedHistory.IsEmpty())SpeedHistory.Add({C->CombatTime()-2.,MoveSpeedMultiplier});
    if(MoveSpeedMultiplier!=Speed)
    {
        MoveSpeedMultiplier=Speed;++MovementConfigRevision;MovementConfigTime=C->CombatTime();
        SpeedHistory.Add({MovementConfigTime,Speed});
        while(SpeedHistory.Num()>2&&(SpeedHistory.Num()>64||SpeedHistory[1].Time<C->CombatTime()-2.))SpeedHistory.RemoveAt(0);
    }
    ActionSpeedMultiplier=float(PS->ResolvedAttributes.Values.FindRef(TEXT("ActionSpeed")));
    MovementConfig.Speed=MoveSpeedMultiplier;MovementConfig.ActionSpeed=ActionSpeedMultiplier;MovementConfig.Revision=MovementConfigRevision;MovementConfig.Time=MovementConfigTime;
    Snapshot=MoveTemp(Next);C->ForceNetUpdate();
}
bool UAetherBuffRuntime::SynchronizeLife()
{
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C||!C->HasAuthority())return false;
    FGuid Life;
    if(!AetherSkillLives::Resolve(*C,Life))
    {
        if(!C->Alive())
        {
            if(!State.Instances.IsEmpty()){State.Instances.Reset();++State.Revision;}
            PendingDueEvents=0;SchedulingState.Reset();bPublicationPending=false;PublishTags();
        }
        return false;
    }
    if(State.LifeId!=Life)
    {
        State={};State.LifeId=Life;PendingDueEvents=0;SchedulingState.Reset();bPublicationPending=false;PublishTags();
    }
    FGuid Current;return AetherSkillLives::Resolve(*C,Current)&&Current==Life&&State.LifeId==Life;
}
bool UAetherBuffRuntime::HasDue() const
{
    const auto* C=Cast<AAetherCharacter>(GetOwner());FGuid Life;
    if(!C||!C->HasAuthority()||!AetherSkillLives::Resolve(*C,Life)||State.LifeId!=Life)return false;
    const double Now=C->CombatTime();
    for(const auto& I:State.Instances)if(I.ExpiresAt<=Now||(I.NextTickAt>0&&I.NextTickAt<=Now))return true;
    return PendingDueEvents>0||bPublicationPending;
}
void UAetherBuffRuntime::Advance(double Now,FGuid ExpectedLife)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C||!C->HasAuthority()||bPublishing)return;
    const auto SameLife=[&]{FGuid Current;return State.LifeId==ExpectedLife&&AetherSkillLives::Resolve(*C,Current)&&Current==ExpectedLife;};
    if(!SameLife())return;
    FAetherBuffDueEvent E;bool Changed=false;
    // At most 64 instances * 36000 periods in a complete lifetime. No expired tick is invented.
    // Drain a bounded number each frame; actions remain blocked while due work remains.
    for(int32 Budget=0;Budget<64&&SameLife()&&State.NextDue(Now,E);++Budget)
    {
        Changed=true;
        if(E.bExpiry){if(!Publish()||!SameLife())return;continue;}
        for(const auto& O:E.Operations)
        {
            if(!C->Alive()||!SameLife())break;
            if(O.Kind==EAetherBuffOperation::Heal)C->SetVitals(C->Health()+float(O.Value*E.Stacks),C->Mana(),C->Stamina());
            else if(O.Kind==EAetherBuffOperation::Damage){FDamageEvent Damage;C->TakeDamage(float(O.Value*E.Stacks),Damage,nullptr,nullptr);}
        }
    }
    if(Changed&&SameLife())Publish();
}
void UAetherBuffRuntime::FlushDue()
{
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C||!C->HasAuthority()||bPublishing)return;
    if(!SynchronizeLife())return;
    const FGuid Life=State.LifeId;const double Now=C->CombatTime();
    if(C->ResourceGate->IsBlocked())
    {
        if(!SchedulingState.IsSet())SchedulingState=State;
        FAetherBuffDueEvent E;const TWeakObjectPtr<UAetherBuffRuntime> Self=this;
        for(int32 Budget=0;Budget<64&&SchedulingState->NextDue(Now,E);++Budget)
        {
            ++PendingDueEvents;
            C->ResourceGate->Defer([Self,At=E.Time,Life]{if(Self.IsValid()){
                const auto* Owner=Cast<AAetherCharacter>(Self->GetOwner());FGuid Current;
                if(!Owner||!AetherSkillLives::Resolve(*Owner,Current)||Current!=Life||Self->State.LifeId!=Life)return;
                --Self->PendingDueEvents;Self->Advance(At,Life);
                if(Self->State.LifeId==Life&&Self->PendingDueEvents==0)Self->SchedulingState.Reset();
            }},E.bExpiry?EAetherEffectEventKind::BuffExpire:EAetherEffectEventKind::BuffTick);
        }
        return;
    }
    // Queued deadlines must keep their positions relative to damage and treatment.
    if(PendingDueEvents==0){SchedulingState.Reset();Advance(Now,Life);}
}
bool UAetherBuffRuntime::Apply(const FString& Id,const FString& Source,FString& Why,FGuid Delivery)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());
    if(!C||!C->HasAuthority()||!C->Alive()||C->ResourceGate->IsBlocked()||bPublishing)
    {Why=TEXT("效果正在结算，请稍后重试。");return false;}
    if(!SynchronizeLife()){Why=TEXT("当前技能生命或权威尚未就绪。");return false;}
    FlushDue();
    const auto& Definitions=FAetherBuffDefinitions::Get();const auto* D=Definitions.bValid?Definitions.Buffs.Find(Id):nullptr;
    if(HasDue()){Why=TEXT("效果正在按顺序结算。");return false;}
    if(!D){Why=TEXT("效果定义不可用。");return false;}
    if(C->SkillAuthority==EAetherSkillAuthority::Definition&&!DefinitionEffectSupported(*D,&Why))return false;
    const auto Previous=State;const auto Result=State.Apply(*D,Source,C->CombatTime(),Delivery);
    if(Result==EAetherBuffResult::Replayed){Why=TEXT("该效果已经生效。");return true;}
    if(Result!=EAetherBuffResult::Applied&&Result!=EAetherBuffResult::Refreshed)
    {Why=Result==EAetherBuffResult::Immune?TEXT("当前免疫此效果。"):TEXT("效果层数已满或施加条件不满足。");return false;}
    if(!CanProject(*C,State,&Why)){State=Previous;return false;}
    if(!Publish()){State=Previous;Publish();Why=TEXT("效果发布暂不可用。");return false;}
    Why=D->DisplayName+TEXT("已生效。");return true;
}
bool UAetherBuffRuntime::Dispel(const FString& Tag,FString& Why)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C||!C->HasAuthority()||!C->Alive()||C->ResourceGate->IsBlocked()||bPublishing)return false;
    if(!SynchronizeLife()){Why=TEXT("当前技能生命或权威尚未就绪。");return false;}
    FlushDue();if(HasDue()){Why=TEXT("效果正在按顺序结算。");return false;}
    const auto Previous=State;
    if(!State.Dispel(Tag)){Why=TEXT("没有可净化的效果；身体温度与水量不受普通净化影响。");return false;}
    const bool Applied=Publish();if(!Applied){State=Previous;Publish();}
    Why=Applied?TEXT("已净化可驱散的减益。"):TEXT("净化暂不可用，请重试。");return Applied;
}
bool UAetherBuffRuntime::ApplyRestBlessing(FString& Why)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());
    if(C&&C->SkillAuthority!=EAetherSkillAuthority::Profile){Why=TEXT("旅舍技能祝福需要Profile权威，Definition不支持技能授予效果。");return false;}
    if(!C||!C->HasAuthority()||!C->Alive()||C->ResourceGate->IsBlocked()||bPublishing)return false;
    const auto* Receiver=C->ResourceGate->GetReceiver();if(!Receiver)return false;
    if(State.LifeId!=Receiver->State().LifeId){State={};State.LifeId=Receiver->State().LifeId;}
    FlushDue();if(HasDue()){Why=TEXT("效果正在按顺序结算。");return false;}auto Candidate=State;
    const auto& Definitions=FAetherBuffDefinitions::Get();
    for(const FString Id:{FString(TEXT("Inn.WaterTraining")),FString(TEXT("Inn.WaterWard"))})
    {
        const auto* D=Definitions.bValid?Definitions.Buffs.Find(Id):nullptr;if(!D){Why=TEXT("旅舍祝福定义不可用。");return false;}
        const auto Result=Candidate.Apply(*D,Id,C->CombatTime());
        if(Result!=EAetherBuffResult::Applied&&Result!=EAetherBuffResult::Refreshed){Why=TEXT("无法接受旅舍祝福。");return false;}
    }
    const auto Previous=State;State=MoveTemp(Candidate);
    if(!Publish()){State=Previous;Publish();Why=TEXT("祝福暂不可用。");return false;}
    Why.Reset();return true;
}
bool UAetherBuffRuntime::CanApply(const FString& Id,FString* Why) const
{
    const auto Reject=[&](const TCHAR* Message){if(Why)*Why=Message;return false;};
    const auto* C=Cast<AAetherCharacter>(GetOwner());
    if(!C||!C->HasAuthority()||!C->Alive()||C->ResourceGate->IsBlocked()||HasDue())return Reject(TEXT("效果受体不可用或正在结算。"));
    FGuid Life;if(!AetherSkillLives::Resolve(*C,Life))return Reject(TEXT("当前技能生命或权威尚未就绪。"));
    auto Candidate=State.LifeId==Life?State:FAetherBuffState();Candidate.LifeId=Life;
    const auto& Definitions=FAetherBuffDefinitions::Get();const auto* D=Definitions.bValid?Definitions.Buffs.Find(Id):nullptr;
    if(!D)return Reject(TEXT("效果定义不可用。"));
    if(C->SkillAuthority==EAetherSkillAuthority::Definition&&!DefinitionEffectSupported(*D,Why))return false;
    const auto Result=Candidate.Apply(*D,TEXT("Delivery.Preflight"),C->CombatTime(),FGuid::NewGuid());
    if(Result!=EAetherBuffResult::Applied&&Result!=EAetherBuffResult::Refreshed)return Reject(TEXT("效果免疫、层数或重施策略拒绝。"));
    if(!CanProject(*C,Candidate,Why))return false;if(Why)Why->Reset();return true;
}
bool UAetherBuffRuntime::ApplyDelivery(const FAetherConsumableEffectV10& E)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());
    if(!C||!C->HasAuthority()||C->SkillAuthority!=EAetherSkillAuthority::Profile||!C->ResourceGate->IsEffectProjection())return false;
    const auto* Receiver=C->ResourceGate->GetReceiver();if(!Receiver)return false;
    // Explicit ExpiredLife closure: never grant a pending old-life buff to a replacement Pawn.
    if(E.Before.LifeId!=Receiver->State().LifeId)
    {UE_LOG(LogTemp,Display,TEXT("AETHER_BUFF_DELIVERY ExpiredLife %s"),*E.DeliveryId.ToString());return true;}
    if(State.LifeId!=Receiver->State().LifeId){State={};State.LifeId=Receiver->State().LifeId;}
    const auto* Definition=FAetherBuffDefinitions::Get().Buffs.Find(E.BuffId);
    if(!Definition||Definition->Revision!=E.BuffRevision)return false;
    const FDateTime Now=FDateTime::UtcNow();const int64 UnixMs=Now.ToUnixTimestamp()*1000+Now.GetMillisecond();
    const double Remaining=double(E.BuffExpiresAtUnixMs-UnixMs)*.001;
    if(Remaining<=0)return true;
    if(Remaining>Definition->Duration+.001)return false;
    auto D=*Definition;D.Duration=Remaining;
    const auto Result=State.Apply(D,TEXT("Delivery.")+E.DeliveryId.ToString(EGuidFormats::Digits),C->CombatTime(),E.DeliveryId);
    if(Result==EAetherBuffResult::Replayed)return Publish();
    return (Result==EAetherBuffResult::Applied||Result==EAetherBuffResult::Refreshed)&&Publish();
}
void UAetherBuffRuntime::RemoveSource(const FString& Source)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C||!C->HasAuthority())return;
    FGuid Life;if(!AetherSkillLives::Resolve(*C,Life))return;
    const TWeakObjectPtr<UAetherBuffRuntime> Self=this;
    if(C->ResourceGate->Defer([Self,Source,Life]{if(Self.IsValid()){
        const auto* Owner=Cast<AAetherCharacter>(Self->GetOwner());FGuid Current;
        if(Owner&&AetherSkillLives::Resolve(*Owner,Current)&&Current==Life)Self->RemoveSource(Source);
    }}))return;
    if(State.RemoveSource(Source))Publish();
}
void UAetherBuffRuntime::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,Type,Function);auto* C=Cast<AAetherCharacter>(GetOwner());if(!C||!C->HasAuthority())return;
    if(!SynchronizeLife())return;
    if(bPublicationPending&&!C->ResourceGate->IsBlocked()&&!Publish())return;
    if(!C->Alive()&&!State.Instances.IsEmpty())
    {
        if(C->ResourceGate->IsBlocked())return;
        State.Instances.Reset();++State.Revision;Publish();return;
    }
    FlushDue();
    if(!C->ResourceGate->IsBlocked())RefreshSnapshot();
}
