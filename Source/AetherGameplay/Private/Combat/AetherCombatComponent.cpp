#include "Combat/AetherCombatComponent.h"
#include "Combat/AetherCombat.h"
#include "Combat/AetherEquipmentMath.h"
#include "Equipment/AetherElementDamage.h"
#include "Inventory/AetherResourceGate.h"
#include "Movement/AetherDodgeAbility.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
UAetherCombatComponent::UAetherCombatComponent()
{
    SetIsReplicatedByDefault(true);PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.1f;
}
void UAetherCombatComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,Type,Function);
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C||!C->HasAuthority())return;
    if(!C->Alive()){StatusEffects.Reset();return;}
    // 这里只镜像既有战斗规则，不另造叠层、伤害或状态计时器。
    const auto Update=[&](FName Kind,bool Active,double End=0)
    {
        const int32 Index=StatusEffects.IndexOfByPredicate([&](const auto& S){return S.Kind==Kind;});
        if(!Active){if(Index!=INDEX_NONE)StatusEffects.RemoveAt(Index);return;}
        if(Index==INDEX_NONE){FAetherBodyStatusPresentation S;S.InstanceId=FGuid::NewGuid();S.Kind=Kind;S.ExpiresAt=End;StatusEffects.Add(S);}
        else StatusEffects[Index].ExpiresAt=End;
    };
    Update(TEXT("Heat"),C->Reactive&&C->Reactive->State.TemperatureC>55);
    Update(TEXT("Frozen"),C->Reactive&&C->Reactive->State.IceFraction>.5);
    Update(TEXT("Wet"),C->Reactive&&C->Reactive->State.ElectricalWetness01>.05);
    Update(TEXT("Stun"),C->CombatTime()<C->StunUntil,C->StunUntil);
}
void UAetherCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(UAetherCombatComponent,StatusEffects);DOREPLIFETIME(UAetherCombatComponent,LastDamageAt);DOREPLIFETIME(UAetherCombatComponent,DamageReceivedCount);}
FAetherDefenseSnapshot UAetherCombatComponent::CaptureDefense(AActor* Source) const
{
    FAetherDefenseSnapshot S;const auto* C=Cast<AAetherCharacter>(GetOwner());if(!C)return S;
    S.Time=C->CombatTime();S.Armor=C->Attributes->GearArmor.GetCurrentValue();
    S.Fire=C->Attributes->GearFireResist.GetCurrentValue();S.Water=C->Attributes->GearWaterResist.GetCurrentValue();
    S.Frost=C->Attributes->GearFrostResist.GetCurrentValue();S.Storm=C->Attributes->GearStormResist.GetCurrentValue();
    S.bInvulnerable=S.Time<InvulnerableUntil||C->AbilitySystem->HasMatchingGameplayTag(AetherDodge::InvulnerableTag());
    const auto* Guard=C->Equipment->GuardDefinition();
    S.bBlocked=C->bBlocking&&Guard&&Source&&FVector::DotProduct(C->GetActorForwardVector(),(Source->GetActorLocation()-C->GetActorLocation()).GetSafeNormal())>.25;
    if(Guard){S.GuardCost=Guard->GuardStaminaMultiplier;S.bParry=S.bBlocked&&S.Time-BlockStarted<Guard->ParryWindowSeconds;}
    return S;
}
void UAetherCombatComponent::ReceiveHit(float Damage,float PostureDamage,AAetherCharacter* Source,bool CanBlock)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C)return;
    if(C->ResourceGate->IsBlocked())
    {
        TWeakObjectPtr<AAetherCharacter> Self=C,Other=Source;
        const auto Defense=CaptureDefense(Source);
        if(C->ResourceGate->Defer([Self,Other,Damage,PostureDamage,CanBlock,Defense]{if(Self.IsValid()){
            TGuardValue<TOptional<FAetherDefenseSnapshot>> Guard(Self->CombatRuntime->DeferredDefense,Defense);
            Self->ReceiveHit(Damage,PostureDamage,Other.Get(),CanBlock);
        }},EAetherEffectEventKind::Damage))return;
    }
    const auto Defense=DeferredDefense.IsSet()?DeferredDefense.GetValue():CaptureDefense(Source);
    if(!C->HasAuthority()||!C->Alive()||Defense.bInvulnerable)return;
    TGuardValue<TOptional<FAetherDefenseSnapshot>> Frozen(DeferredDefense,Defense);
    const float T=C->CombatTime();
    if(CanBlock&&Defense.bBlocked)
    {
        C->RecordEquipmentWear(false,true);
        if(Defense.bParry){if(IsValid(Source)&&Source->Alive()){Source->StunUntil=T+1.1f;Source->CancelActions();}return;}
        const float Cost=PostureDamage*Defense.GuardCost;
        if(C->Stamina()>=Cost){C->AbilitySystem->ApplyModToAttribute(UAetherAttributes::GetStaminaAttribute(),EGameplayModOp::Additive,-Cost);return;}
        C->bBlocking=false;C->StunUntil=T+1.2f;C->CancelActions();
    }
    ApplyPostureDamage(PostureDamage);FDamageEvent Event;
    // 仍经过角色虚函数，保留衍生角色的信用/遭遇和伤害接收钩子。
    C->TakeDamage(Damage,Event,Source?Source->GetController():nullptr,Source);
}
void UAetherCombatComponent::ApplyPostureDamage(float Amount)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C)return;
    if(C->ResourceGate->IsBlocked())
    {
        TWeakObjectPtr<AAetherCharacter> Self=C;
        if(C->ResourceGate->Defer([Self,Amount]{if(Self.IsValid())Self->ApplyPostureDamage(Amount);}))return;
    }
    if(!C->HasAuthority()||!C->AbilitySystem||C->AbilitySystem->GetAvatarActor()!=C||!C->Alive()||!FMath::IsFinite(Amount)||Amount<=0)return;
    float Posture=C->Attributes->Posture.GetCurrentValue()+(C->bUseBasicAssets?-Amount:Amount);
    if(C->bUseBasicAssets?Posture<=0:Posture>=100){C->StunUntil=C->CombatTime()+1.5f;Posture=0;C->bBlocking=false;C->CancelActions();}
    C->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetPostureAttribute(),Posture);
}
float UAetherCombatComponent::ApplyDamage(float Amount,const FDamageEvent& Event,AController* Instigator,AActor* Causer)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C||C->DeferDamage(Amount,Event,Instigator,Causer))return 0;
    if(!C->HasAuthority()||!C->AbilitySystem||C->AbilitySystem->GetAvatarActor()!=C||!C->Alive()||!FMath::IsFinite(Amount)||Amount<=0)return 0;
    const auto Type=Event.DamageTypeClass;float Applied=Amount;
    const auto Defense=DeferredDefense.IsSet()?DeferredDefense.GetValue():CaptureDefense(Causer);
    if(Type&&Type->IsChildOf(UAetherFireDamage::StaticClass()))Applied*=AetherEquipmentMath::ElementMultiplier(Defense.Fire);
    else if(Type&&Type->IsChildOf(UAetherWaterDamage::StaticClass()))Applied*=AetherEquipmentMath::ElementMultiplier(Defense.Water);
    else if(Type&&Type->IsChildOf(UAetherFrostDamage::StaticClass()))Applied*=AetherEquipmentMath::ElementMultiplier(Defense.Frost);
    else if(Type&&Type->IsChildOf(UAetherStormDamage::StaticClass()))Applied*=AetherEquipmentMath::ElementMultiplier(Defense.Storm);
    else Applied=AetherEquipmentMath::PhysicalDamage(Amount,Defense.Armor);
    Applied=FMath::Min(C->Health(),Applied);
    C->AbilitySystem->ApplyModToAttribute(UAetherAttributes::GetHealthAttribute(),EGameplayModOp::Additive,-Applied);
    if(Applied>0){
        // 一次武器命中仍磨损一次；持续热暴露每累计一点有效伤害磨损一次，30/60/120 FPS 一致。
        const int32 Wear=Type&&Type->IsChildOf(UAetherHeatExposureDamage::StaticClass())?AetherEquipmentMath::ContinuousWear(Applied,HeatWearRemainder):1;
        for(int32 I=0;I<Wear;++I)C->RecordEquipmentWear(false,false);
    }
    LastDamager=Causer;++DamageReceivedCount;LastDamageAt=Defense.Time;
    if(Applied>0&&C->CastExecutionId.IsValid())C->CancelActions();
    if(Applied>0&&C->Alive())C->PresentAction(TEXT("Hit"));
    if(!C->Alive()){C->CancelActions();C->bBlocking=C->bWindingUp=false;C->GetCharacterMovement()->StopMovementImmediately();}
    return Applied;
}
