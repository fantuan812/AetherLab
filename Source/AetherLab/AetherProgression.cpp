#include "AetherProgression.h"
#include "AetherRules.h"
#include "AetherInventoryRules.h"
#include "AetherQuestRuntime.h"
#include "Net/UnrealNetwork.h"

int32 FAetherProfile::MaxStack(FName Id, const FAetherRules& Rules)
{const auto* R=Rules.Items.Find(Id);return R?R->MaxStack:0;}
int32 FAetherProfile::Count(FName Id) const
{ int32 N=0; for (const auto& S:Inventory) if (S.DefinitionId==Id) N+=S.Count; return N; }
bool FAetherProfile::Add(FName Id, int32 Q, const FAetherRules& Rules)
{
    const int32 Limit=MaxStack(Id,Rules); if (Limit==0 || Q<=0 || Q>1000) return false;
    auto Next=Inventory;
    for (auto& S:Next) if (S.DefinitionId==Id) { const int32 N=FMath::Min(Q,Limit-S.Count); S.Count+=N; Q-=N; }
    while (Q>0) { if (Next.Num()>=32) return false; FAetherItemStack S; S.InstanceId=FGuid::NewGuid(); S.DefinitionId=Id; S.Count=FMath::Min(Q,Limit); Q-=S.Count; Next.Add(S); }
    Inventory=MoveTemp(Next); return true;
}
bool FAetherProfile::Remove(FName Id, int32 Q, const FAetherRules& Rules)
{
    const auto* Rule=Rules.Items.Find(Id);
    if (!Rule || !Rule->bRemovable || Q<=0 || Count(Id)<Q) return false;
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
bool FAetherProfile::Merge(FGuid From, FGuid To, const FAetherRules& Rules)
{
    if (From==To || Equipped.FindKey(From) || Equipped.FindKey(To)) return false;
    auto* A=Inventory.FindByPredicate([From](const auto& S){return S.InstanceId==From;});
    auto* B=Inventory.FindByPredicate([To](const auto& S){return S.InstanceId==To;});
    if (!A || !B || A->DefinitionId!=B->DefinitionId) return false;
    const int32 N=FMath::Min(A->Count,MaxStack(A->DefinitionId,Rules)-B->Count); if (N<=0) return false;
    A->Count-=N; B->Count+=N; Inventory.RemoveAll([](const auto& S){return S.Count==0;}); return true;
}
bool FAetherProfile::Equip(FGuid Id,const FAetherRules& R){return AetherInventory::Equip(*this,Id,R);}
FName FAetherProfile::QuestId(int32 Q){const auto& R=FAetherRules::Get();return R.Quests.IsValidIndex(Q)?R.Quests[Q].Id:NAME_None;}
FString FAetherProfile::QuestTitle(FName Id,const FAetherRules& R){const auto* Q=R.Quest(Id);return Q?Q->Title:TEXT("Missing quest definition");}
TArray<FName> FAetherProfile::Objectives(FName Id,const FAetherRules& R){const auto* Q=R.Quest(Id);return Q?Q->Objectives:TArray<FName>();}
bool FAetherProfile::Available(FName Id,const FAetherRules& R) const{return AetherQuests::Available(*this,Id,R);}
bool FAetherProfile::Complete(FName Id,const FAetherRules& R) const{return AetherQuests::Complete(*this,Id,R);}
bool FAetherProfile::Observe(FName Fact,const FAetherRules& R){return AetherQuests::Observe(*this,Fact,R);}
bool FAetherProfile::Claim(FName Id,const FAetherRules& R){return AetherQuests::Claim(*this,Id,R);}
bool FAetherProfile::Validate(const FAetherRules& Rules) const
{
    if (PendingGold<0||PendingGold>1000000||PendingMaterial<0||PendingMaterial>1000||DailyEvidence.Num()>64||DailyClaims.Num()>16||DailyDate.Len()>8||Inventory.Num()>32 || Evidence.Num()>512 || Claims.Num()>512 || Gold<0 || Gold>10000000 || Revision<0 || Experience<0 || LearnedSpells>15 || CharacterId.Len()>32) return false;
    TSet<FGuid> IDs; for (const auto& S:Inventory)
    { if (!S.InstanceId.IsValid() || IDs.Contains(S.InstanceId) || S.Count<=0 || S.Count>MaxStack(S.DefinitionId,Rules)) return false; IDs.Add(S.InstanceId); }
    TSet<FName> Unique; for (auto F:Claims) { if (Unique.Contains(F)) return false; Unique.Add(F); }
    return AetherInventory::ValidateLoadout(*this,Rules);
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
    if(!Claims.Contains("Q_Main_08")||Template<0||Template>2)return false;
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

bool FAetherProfile::TryAutoClaim(FName Id){const auto* Q=FAetherRules::Get().Quest(Id);return Q&&Q->bAutoClaim&&Claim(Id);}
