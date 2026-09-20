#include "Equipment/AetherEquipmentEffect.h"
#include "AetherCombat.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GearDamage,"Aether.Equipment.Damage");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GearPosture,"Aether.Equipment.Posture");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GearArmor,"Aether.Equipment.Armor");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GearFireResist,"Aether.Equipment.FireResist");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GearWaterResist,"Aether.Equipment.WaterResist");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GearFrostResist,"Aether.Equipment.FrostResist");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GearStormResist,"Aether.Equipment.StormResist");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GearMaxHealth,"Aether.Equipment.MaxHealth");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GearMaxMana,"Aether.Equipment.MaxMana");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GearMaxStamina,"Aether.Equipment.MaxStamina");
namespace
{
struct FStat {const TCHAR* Id;FGameplayTag Tag;FGameplayAttribute Attribute;};
TArray<FStat> Binding()
{
    return {
        {TEXT("Damage"),TAG_GearDamage,UAetherAttributes::GetGearDamageAttribute()},
        {TEXT("Posture"),TAG_GearPosture,UAetherAttributes::GetGearPostureAttribute()},
        {TEXT("Armor"),TAG_GearArmor,UAetherAttributes::GetGearArmorAttribute()},
        {TEXT("FireResist"),TAG_GearFireResist,UAetherAttributes::GetGearFireResistAttribute()},
        {TEXT("WaterResist"),TAG_GearWaterResist,UAetherAttributes::GetGearWaterResistAttribute()},
        {TEXT("FrostResist"),TAG_GearFrostResist,UAetherAttributes::GetGearFrostResistAttribute()},
        {TEXT("StormResist"),TAG_GearStormResist,UAetherAttributes::GetGearStormResistAttribute()},
        {TEXT("MaxHealth"),TAG_GearMaxHealth,UAetherAttributes::GetGearMaxHealthAttribute()},
        {TEXT("MaxMana"),TAG_GearMaxMana,UAetherAttributes::GetGearMaxManaAttribute()},
        {TEXT("MaxStamina"),TAG_GearMaxStamina,UAetherAttributes::GetGearMaxStaminaAttribute()}
    };
}
}
UAetherEquipmentEffect::UAetherEquipmentEffect()
{
    DurationPolicy=EGameplayEffectDurationType::Infinite;
    for(const auto& S:Binding())
    {
        FGameplayModifierInfo M;M.Attribute=S.Attribute;M.ModifierOp=EGameplayModOp::Additive;
        FSetByCallerFloat Value;Value.DataTag=S.Tag;M.ModifierMagnitude=FGameplayEffectModifierMagnitude(Value);Modifiers.Add(M);
    }
}
bool AetherEquipmentEffects::Publish(UAbilitySystemComponent& ASC,FActiveGameplayEffectHandle& Source,
    const TMap<FString,double>& Stats,FString& Reason)
{
    if(!ASC.IsOwnerActorAuthoritative()){Reason=TEXT("Equipment effects require authority");return false;}
    const auto Fields=Binding();TMap<FGameplayTag,float> Values;
    for(const auto& P:Stats)
        if(!Fields.ContainsByPredicate([&](const auto& F){return P.Key.Equals(F.Id,ESearchCase::CaseSensitive);})||
            !FMath::IsFinite(P.Value)||P.Value<0||P.Value>100000)
        {Reason=TEXT("Unknown or invalid equipped statistic");return false;}
    for(const auto& F:Fields)Values.Add(F.Tag,float(Stats.FindRef(F.Id)));
    // 外部清除效果后可重建；持续 ASC 上同一个来源只更新 SetByCaller，不累计句柄。
    if(const auto* Active=Source.IsValid()?ASC.GetActiveGameplayEffect(Source):nullptr)
    {
        bool Changed=false;
        for(const auto& P:Values){const auto* Old=Active->Spec.SetByCallerTagMagnitudes.Find(P.Key);Changed|=!Old||*Old!=P.Value;}
        if(Changed)ASC.UpdateActiveGameplayEffectSetByCallerMagnitudes(Source,Values);
    }
    else
    {
        auto Spec=ASC.MakeOutgoingSpec(UAetherEquipmentEffect::StaticClass(),1,ASC.MakeEffectContext());
        if(!Spec.IsValid()){Reason=TEXT("Cannot construct equipment effect");return false;}
        for(const auto& P:Values)Spec.Data->SetSetByCallerMagnitude(P.Key,P.Value);
        Source=ASC.ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    }
    if(!Source.IsValid()||!ASC.GetActiveGameplayEffect(Source)){Reason=TEXT("Equipment source effect was rejected");return false;}
    Reason.Reset();return true;
}
