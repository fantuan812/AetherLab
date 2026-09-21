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
void UAetherCombatComponent::ReceiveHit(float Damage,float PostureDamage,AAetherCharacter* Source,bool CanBlock)
{
    auto* C=Cast<AAetherCharacter>(GetOwner());if(!C)return;
    if(C->ResourceGate->IsBlocked())
    {
        TWeakObjectPtr<AAetherCharacter> Self=C,Other=Source;
        if(C->ResourceGate->Defer([Self,Other,Damage,PostureDamage,CanBlock]{if(Self.IsValid())Self->ReceiveHit(Damage,PostureDamage,Other.Get(),CanBlock);}))return;
    }
    if(!C->HasAuthority()||!C->Alive()||C->CombatTime()<InvulnerableUntil||C->AbilitySystem->HasMatchingGameplayTag(AetherDodge::InvulnerableTag()))return;
    const float T=C->CombatTime();const bool Front=Source&&FVector::DotProduct(C->GetActorForwardVector(),(Source->GetActorLocation()-C->GetActorLocation()).GetSafeNormal())>.25;
    const auto* Guard=C->Equipment->GuardDefinition();
    if(CanBlock&&C->bBlocking&&Front&&Guard)
    {
        C->RecordEquipmentWear(false,true);
        if(T-BlockStarted<Guard->ParryWindowSeconds){Source->StunUntil=T+1.1f;Source->CancelActions();return;}
        const float Cost=PostureDamage*Guard->GuardStaminaMultiplier;
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
    if(Type&&Type->IsChildOf(UAetherFireDamage::StaticClass()))Applied*=AetherEquipmentMath::ElementMultiplier(C->Attributes->GearFireResist.GetCurrentValue());
    else if(Type&&Type->IsChildOf(UAetherWaterDamage::StaticClass()))Applied*=AetherEquipmentMath::ElementMultiplier(C->Attributes->GearWaterResist.GetCurrentValue());
    else if(Type&&Type->IsChildOf(UAetherFrostDamage::StaticClass()))Applied*=AetherEquipmentMath::ElementMultiplier(C->Attributes->GearFrostResist.GetCurrentValue());
    else if(Type&&Type->IsChildOf(UAetherStormDamage::StaticClass()))Applied*=AetherEquipmentMath::ElementMultiplier(C->Attributes->GearStormResist.GetCurrentValue());
    else Applied=AetherEquipmentMath::PhysicalDamage(Amount,C->Attributes->GearArmor.GetCurrentValue());
    Applied=FMath::Min(C->Health(),Applied);
    C->AbilitySystem->ApplyModToAttribute(UAetherAttributes::GetHealthAttribute(),EGameplayModOp::Additive,-Applied);
    if(Applied>0)C->RecordEquipmentWear(false,false);
    LastDamager=Causer;++DamageReceivedCount;LastDamageAt=C->CombatTime();
    if(Applied>0&&C->Alive())C->PresentAction(TEXT("Hit"),.35f);
    if(!C->Alive()){C->CancelActions();C->bBlocking=C->bWindingUp=false;C->GetCharacterMovement()->StopMovementImmediately();}
    return Applied;
}
