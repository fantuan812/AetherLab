#include "World/AetherWorldState.h"
namespace
{
bool Id(const FString& S,bool Empty=false)
{
    if(S.IsEmpty())return Empty;if(S.Len()>128)return false;
    for(TCHAR C:S)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))return false;
    return true;
}
bool Character(const FString& S,bool Empty=false)
{
    if(S.IsEmpty())return Empty;if(S.Len()>32)return false;
    for(TCHAR C:S)if(C<32)return false;return true;
}
bool Number(double V,double Min,double Max){return FMath::IsFinite(V)&&V>=Min&&V<=Max;}
bool Encounter(const FAetherEncounterStateV10& E,const TCHAR* Expected)
{
    if((E.Definition!=Expected&&!E.Definition.IsEmpty())||E.Phase>6||E.Version<0||E.Wave<0||E.Wave>3||
        E.LockedSeats<2||E.LockedSeats>4||!Number(E.PhaseStarted,0,1.e12)||!Number(E.Progress,0,1000000)||
        E.Participants.Num()>128||E.Settled.Num()>E.Participants.Num())return false;
    // 累计参与者可以超过四人（公共事件有人离开又有人加入），同时锁定席位仍不超过四。
    TSet<FString> Participants,Settled;
    for(const auto& Id:E.Participants){if(!Character(Id)||Participants.Contains(Id))return false;Participants.Add(Id);}
    for(const auto& Id:E.Settled){if(!Participants.Contains(Id)||Settled.Contains(Id))return false;Settled.Add(Id);}
    if(E.Phase!=0&&(!E.Instance.IsValid()||E.Definition.IsEmpty()||Participants.IsEmpty()))return false;
    if(!E.Instance.IsValid()&&(!Participants.IsEmpty()||E.Phase!=0||!Settled.IsEmpty()))return false;
    return true;
}
}
bool FAetherWorldStateV10::Validate(const FAetherV10ItemDefinitions& Items,const FAetherRules& Rules,
    const TMap<FString,int64>& Profiles,FString& Reason) const
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(!Items.Validate(Reason))return false;
    if(!Rules.bValid||Revision<0||Revision==MAX_int64||Bodies.Num()>4096||Loot.Num()>128||CampReceipts.Num()>32||
        LegacyServiceReceipts.Num()>64||WorldFactSources.Num()>512)return Fail(TEXT("Invalid world schema/count/version"));
    if(!Number(RainKgPerM2Sec,0,1)||!Number(AmbientTemperatureC,-100,100)||WindMPerSec.ContainsNaN()||WindMPerSec.Size()>100)
        return Fail(TEXT("Invalid persisted environment"));
    TSet<FString> StableIds;
    for(const auto& B:Bodies)
    {
        if(!Id(B.StableId)||!Id(B.RegionId,true)||StableIds.Contains(B.StableId)||!B.Transform.IsValid()||
            B.Transform.GetLocation().GetAbsMax()>1.e8||B.Transform.GetScale3D().GetMin()<=0||B.Transform.GetScale3D().GetMax()>1000||
            (B.MaterialSchema!=0&&B.MaterialSchema!=1)||!Number(B.RemainingEnergyJ,0,1.e12)||!Number(B.SourceAge,0,1.e12)||
            !Number(B.EnthalpyJ,-1.e12,1.e12)||!Number(B.WaterKg,0,1.e9)||!Number(B.ElectricalWaterKg,0,B.WaterKg)||
            !Number(B.ElectricalWetness01,0,1)||!Number(B.FuelKg,0,1.e9)||!Number(B.Integrity,0,1)||!Number(B.GasEnergyJ,0,1.e12)||
            (B.bBurst&&B.GasEnergyJ>0)||(B.bBroken!=(B.Integrity<=.05)))return Fail(TEXT("Invalid or duplicate reactive body state"));
        StableIds.Add(B.StableId);
    }
    TSet<FGuid> LootIds;
    for(const auto& L:Loot)
    {
        if(!L.ClaimId.IsValid()||LootIds.Contains(L.ClaimId)||L.Location.ContainsNaN()||L.Location.GetAbsMax()>100000||
            !Items.Items.Contains(L.Definition)||L.Count<1||L.Count>99||L.Items.Num()>128||!Character(L.ClaimedBy,true))
            return Fail(TEXT("Invalid/duplicate world loot"));
        for(const auto& P:L.Items)if(!Items.Items.Contains(P.Key)||P.Value<1||P.Value>1000)return Fail(TEXT("Unknown/invalid world loot item"));
        LootIds.Add(L.ClaimId);
    }
    TSet<FString> Camps;TSet<FGuid> Instances;
    for(const auto& C:CampReceipts)
    {
        bool Known=false;for(const auto& R:Rules.Encounters)Known|=R.Key.ToString()==C.Definition;
        if(!Known||Camps.Contains(C.Definition)||!C.Instance.IsValid()||Instances.Contains(C.Instance)||(C.RespawnAfterUtc<0||C.RespawnAfterUtc==MAX_int64))
            return Fail(TEXT("Invalid camp receipt"));
        Camps.Add(C.Definition);Instances.Add(C.Instance);
    }
    TSet<FGuid> Services;
    for(const auto& S:LegacyServiceReceipts)
    {
        const auto* Version=Profiles.Find(S.CharacterId);
        if(!S.CommandId.IsValid()||Services.Contains(S.CommandId)||!Id(S.TargetId)||!Character(S.CharacterId)||!Version||
            S.ExpectedRevision<0||S.ExpectedRevision>=*Version)return Fail(TEXT("Invalid legacy service receipt/profile reference"));
        Services.Add(S.CommandId);
    }
    for(const auto& F:WorldFactSources)
    {
        bool Known=false;
        for(const auto& P:Rules.Objectives)if(P.Key.ToString()==F.Key&&P.Value.Scope==EAetherObjectiveScope::World)
            for(FName Source:P.Value.FactSources)Known|=Source.ToString()==F.Value;
        if(!Known)return Fail(TEXT("Invalid world fact provenance"));
    }
    if(!Encounter(Abbey,TEXT("Abbey"))||!Encounter(Relay,TEXT("Relay")))return Fail(TEXT("Invalid encounter snapshot"));
    if(LegacySaveSchema==0)
    {
        if(LegacyGeneration!=0||!LegacySourceSha256.IsEmpty()||!LegacyServiceReceipts.IsEmpty())return Fail(TEXT("Legacy world data without import provenance"));
    }
    else
    {
        if((LegacySaveSchema!=4&&LegacySaveSchema!=5)||LegacyGeneration<0||Revision<LegacyGeneration||LegacySourceSha256.Len()!=64)
            return Fail(TEXT("Invalid world migration provenance"));
        for(TCHAR C:LegacySourceSha256)if(!((C>='0'&&C<='9')||(C>='a'&&C<='f')))return Fail(TEXT("Invalid world source digest"));
    }
    Reason.Reset();return true;
}
