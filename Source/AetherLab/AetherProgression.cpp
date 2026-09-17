#include "AetherProgression.h"
#include "AetherRules.h"
#include "Net/UnrealNetwork.h"

int32 FAetherProfile::MaxStack(FName Id)
{const auto* R=FAetherRules::Get().Items.Find(Id);return R?R->MaxStack:0;}
int32 FAetherProfile::Count(FName Id) const
{ int32 N=0; for (const auto& S:Inventory) if (S.DefinitionId==Id) N+=S.Count; return N; }
bool FAetherProfile::Add(FName Id, int32 Q)
{
    const int32 Limit=MaxStack(Id); if (Limit==0 || Q<=0 || Q>1000) return false;
    auto Next=Inventory;
    for (auto& S:Next) if (S.DefinitionId==Id) { const int32 N=FMath::Min(Q,Limit-S.Count); S.Count+=N; Q-=N; }
    while (Q>0) { if (Next.Num()>=32) return false; FAetherItemStack S; S.InstanceId=FGuid::NewGuid(); S.DefinitionId=Id; S.Count=FMath::Min(Q,Limit); Q-=S.Count; Next.Add(S); }
    Inventory=MoveTemp(Next); return true;
}
bool FAetherProfile::Remove(FName Id, int32 Q)
{
    if (Q<=0 || Count(Id)<Q || Id=="AncientSeal") return false;
    auto Next=Inventory;
    for (auto& S:Next) if (S.DefinitionId==Id && Q>0)
    {
        if (Equipped.FindKey(S.InstanceId)) continue;
        int32 N=FMath::Min(Q,S.Count); S.Count-=N; Q-=N;
    }
    if (Q) return false;
    Next.RemoveAll([](const auto& S){return S.Count==0;}); Inventory=MoveTemp(Next); return true;
}
bool FAetherProfile::Split(FGuid Id, int32 Q)
{
    if (Inventory.Num()>=32 || Q<=0) return false;
    auto* S=Inventory.FindByPredicate([Id](const auto& V){return V.InstanceId==Id;});
    if (!S || Q>=S->Count || Equipped.FindKey(Id)) return false;
    FAetherItemStack N=*S; N.InstanceId=FGuid::NewGuid(); N.Count=Q; S->Count-=Q; Inventory.Add(N); return true;
}
bool FAetherProfile::Merge(FGuid From, FGuid To)
{
    if (From==To || Equipped.FindKey(From) || Equipped.FindKey(To)) return false;
    auto* A=Inventory.FindByPredicate([From](const auto& S){return S.InstanceId==From;});
    auto* B=Inventory.FindByPredicate([To](const auto& S){return S.InstanceId==To;});
    if (!A || !B || A->DefinitionId!=B->DefinitionId) return false;
    const int32 N=FMath::Min(A->Count,MaxStack(A->DefinitionId)-B->Count); if (N<=0) return false;
    A->Count-=N; B->Count+=N; Inventory.RemoveAll([](const auto& S){return S.Count==0;}); return true;
}
bool FAetherProfile::Equip(FGuid Id)
{
    const auto* S=Inventory.FindByPredicate([Id](const auto& V){return V.InstanceId==Id;}); if (!S) return false;
    FName Slot="MainHand";
    if (S->DefinitionId=="TrainingShield") Slot="OffHand";
    else if (S->DefinitionId!="TrainingSword" && S->DefinitionId!="TrainingHammer" && S->DefinitionId!="TideStaff") return false;
    if (Slot=="OffHand")
        if (const auto* Hand=Equipped.Find("MainHand"))
            for (const auto& I:Inventory) if (I.InstanceId==*Hand && (I.DefinitionId=="TrainingHammer" || I.DefinitionId=="TideStaff")) return false;
    if (S->DefinitionId=="TrainingHammer" || S->DefinitionId=="TideStaff") Equipped.Remove("OffHand");
    Equipped.Add(Slot,Id); return true;
}
FName FAetherProfile::QuestId(int32 Q) { const auto& R=FAetherRules::Get();return R.Quests.IsValidIndex(Q)?R.Quests[Q].Id:NAME_None; }
FString FAetherProfile::QuestTitle(int32 Q) {const auto& R=FAetherRules::Get();return R.Quests.IsValidIndex(Q)?R.Quests[Q].Title:TEXT("Missing quest definition");}
TArray<FName> FAetherProfile::Objectives(int32 Q) {const auto& R=FAetherRules::Get();return R.Quests.IsValidIndex(Q)?R.Quests[Q].Objectives:TArray<FName>();}
bool FAetherProfile::Available(int32 Q) const
{
    const auto& R=FAetherRules::Get();if(!R.bValid||!R.Quests.IsValidIndex(Q)||Claims.Contains(QuestId(Q)))return false;
    for(int32 P:R.Quests[Q].Prerequisites)if(!Claims.Contains(QuestId(P)))return false;return true;
}
bool FAetherProfile::Complete(int32 Q) const
{ if (!Available(Q)) return false; for (FName F:Objectives(Q)) if (!Evidence.Contains(F)) return false; return true; }
bool FAetherProfile::Observe(FName Fact)
{
    if (Evidence.Contains(Fact) || Evidence.Num()>=512) return false;
    for (int32 Q=0;Q<8;++Q) if (Available(Q)&&Objectives(Q).Contains(Fact)) { Evidence.Add(Fact); return true; }
    return false;
}
bool FAetherProfile::Claim(int32 Q)
{
    if (!Complete(Q)) return false;
    auto Next=*this;
    const auto& Rule=FAetherRules::Get().Quests[Q];
    for(auto P:Rule.Items)if(!Next.Add(P.Key,P.Value))return false;
    if(Next.Gold>10000000-Rule.Gold||Next.Experience>MAX_int32-Rule.Experience)return false;
    Next.Claims.Add(QuestId(Q)); Next.Gold+=Rule.Gold; Next.Experience+=Rule.Experience;
    if (Q==1) Next.bRegistered=true;
    *this=MoveTemp(Next); return true;
}
bool FAetherProfile::Validate() const
{
    if (PendingGold<0||PendingGold>1000000||PendingMaterial<0||PendingMaterial>1000||DailyEvidence.Num()>64||DailyClaims.Num()>16||DailyDate.Len()>8||Inventory.Num()>32 || Evidence.Num()>512 || Claims.Num()>512 || Gold<0 || Gold>10000000 || Revision<0 || Experience<0 || LearnedSpells>15 || CharacterId.Len()>32) return false;
    TSet<FGuid> IDs; for (const auto& S:Inventory)
    { if (!S.InstanceId.IsValid() || IDs.Contains(S.InstanceId) || S.Count<=0 || S.Count>MaxStack(S.DefinitionId)) return false; IDs.Add(S.InstanceId); }
    TSet<FName> Unique; for (auto F:Claims) { if (Unique.Contains(F)) return false; Unique.Add(F); }
    auto Check=*this; Check.Equipped.Reset();
    if (Equipped.Num()>2) return false;
    if (auto* Id=Equipped.Find("MainHand")) if (!Check.Equip(*Id)) return false;
    if (auto* Id=Equipped.Find("OffHand")) if (!Check.Equip(*Id)) return false;
    return Check.Equipped.OrderIndependentCompareEqual(Equipped);
}
AAetherPlayerState::AAetherPlayerState()
{
    AbilitySystem=CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("PersistentAbilities")); AbilitySystem->SetIsReplicated(true);
    AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    Attributes=CreateDefaultSubobject<UAetherAttributes>(TEXT("PersistentAttributes")); Attributes->Posture.SetBaseValue(100); Attributes->Posture.SetCurrentValue(100); SetNetUpdateFrequency(20);
}
void AAetherPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME_CONDITION(AAetherPlayerState,Profile,COND_OwnerOnly);DOREPLIFETIME(AAetherPlayerState,DisplayName);DOREPLIFETIME(AAetherPlayerState,PartyLeader);DOREPLIFETIME_CONDITION(AAetherPlayerState,InvitedBy,COND_OwnerOnly); }

bool FAetherProfile::NetSerialize(FArchive& Ar, UPackageMap*, bool& Success)
{ StaticStruct()->SerializeBin(Ar,this); Success=!Ar.IsError(); return true; }

void FAetherProfile::RefreshDaily(const FString& Date)
{if(Date.Len()==8&&Date.IsNumeric()&&(DailyDate.IsEmpty()||Date>DailyDate)){DailyDate=Date;DailyEvidence.Reset();DailyClaims.Reset();}}
bool FAetherProfile::ClaimDaily(int32 Template)
{
    if(!Claims.Contains(QuestId(7))||Template<0||Template>2)return false;
    const FName Key=*FString::Printf(TEXT("Daily%d"),Template);if(DailyClaims.Contains(Key))return false;
    auto Next=*this;
    if(Template==0){if(!Next.Remove("Supply",2))return false;}
    if(Template==1)for(int32 I=0;I<3;++I)if(!DailyEvidence.Contains(*FString::Printf(TEXT("Patrol%d"),I)))return false;
    if(Template==2)for(int32 I=0;I<3;++I)if(!DailyEvidence.Contains(*FString::Printf(TEXT("DailyFire%d"),I)))return false;
    if(!Next.Add("Material",2)||Next.Gold>9999940)return false;
    Next.Gold+=30+Template*15;Next.DailyClaims.Add(Key);*this=MoveTemp(Next);return true;
}

bool FAetherProfile::CollectPending()
{
 if(PendingGold==0&&PendingMaterial==0)return false;
 auto Next=*this;if(Next.Gold>10000000-PendingGold||(PendingMaterial>0&&!Next.Add("Material",PendingMaterial)))return false;
 Next.Gold+=PendingGold;Next.PendingGold=0;Next.PendingMaterial=0;*this=MoveTemp(Next);return true;
}
