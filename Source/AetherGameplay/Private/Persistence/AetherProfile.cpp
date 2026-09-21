#include "Persistence/AetherProfile.h"
#include "Framework/AetherRules.h"
#include "Inventory/AetherInventoryRules.h"
#include "Quests/AetherQuestRuntime.h"


FName FAetherProfile::QuestId(int32 Q){const auto& R=FAetherRules::Get();return R.Quests.IsValidIndex(Q)?R.Quests[Q].Id:NAME_None;}
FString FAetherProfile::QuestTitle(FName Id,const FAetherRules& R){const auto* Q=R.Quest(Id);return Q?Q->Title:TEXT("Missing quest definition");}
TArray<FName> FAetherProfile::Objectives(FName Id,const FAetherRules& R){const auto* Q=R.Quest(Id);return Q?Q->Objectives:TArray<FName>();}
bool FAetherProfile::Available(FName Id,const FAetherRules& R) const{return AetherQuests::Available(*this,Id,R);}
bool FAetherProfile::Complete(FName Id,const FAetherRules& R) const{return AetherQuests::Complete(*this,Id,R);}
bool FAetherProfile::Observe(FName Fact,const FAetherRules& R){return AetherQuests::Observe(*this,Fact,R);}
bool FAetherProfile::Claim(FName Id,const FAetherRules& R){return AetherQuests::Claim(*this,Id,R);}
bool FAetherProfile::Validate(const FAetherRules& Rules) const
{
    if (PendingGold<0||PendingGold>1000000||PendingMaterial<0||PendingMaterial>1000||DailyEvidence.Num()>64||DailyClaims.Num()>16||DailyDate.Len()>8||Inventory.Num()>Rules.InventoryCapacity || Evidence.Num()>512 || Claims.Num()>512 || Gold<0 || Gold>10000000 || Revision<0 || Experience<0 || LearnedSpells>15 || CharacterId.Len()>32) return false;
    if(InventoryReceipts.Num()>64)return false;
    TSet<FGuid> Commands;for(const auto& R:InventoryReceipts){if(!R.Command.CommandId.IsValid()||Commands.Contains(R.Command.CommandId)||R.Command.ExpectedInventoryRevision<0||R.Command.ExpectedInventoryRevision==MAX_int32||R.FinalRevision!=R.Command.ExpectedInventoryRevision+1||R.FinalRevision>Revision+int64(1)||R.Transferred<1||R.Transferred>1000)return false;Commands.Add(R.Command.CommandId);}
    TSet<FGuid> IDs; for (const auto& S:Inventory)
    { if (!S.InstanceId.IsValid() || IDs.Contains(S.InstanceId) || S.Count<=0 || S.Count>MaxStack(S.DefinitionId,Rules)) return false; IDs.Add(S.InstanceId); }
    TSet<FName> Unique; for (auto F:Claims) { if (Unique.Contains(F)) return false; Unique.Add(F); }
    return AetherInventory::ValidateLoadout(*this,Rules);
}
bool FAetherProfile::NetSerialize(FArchive& Ar, UPackageMap*, bool& Success)
{ StaticStruct()->SerializeBin(Ar,this); Success=!Ar.IsError(); return true; }

void FAetherProfile::RefreshDaily(const FString& Date)
{if(Date.Len()==8&&Date.IsNumeric()&&(DailyDate.IsEmpty()||Date>DailyDate)){DailyDate=Date;DailyEvidence.Reset();DailyClaims.Reset();}}
bool FAetherProfile::ClaimDaily(int32 Template)
{
    const auto& R=FAetherRules::Get();if(!R.Dailies.IsValidIndex(Template))return false;const auto& D=R.Dailies[Template];
    if(!Claims.Contains(D.QuestGate)||DailyClaims.Contains(D.Id))return false;
    auto Next=*this;for(auto Fact:D.Facts)if(!DailyEvidence.Contains(Fact))return false;
    for(const auto& Item:D.Consume)if(!Next.Remove(Item.Key,Item.Value,R))return false;
    if(!AetherItems::Grant(Next,D.Reward,D.Gold,R))return false;
    Next.DailyClaims.Add(D.Id);*this=MoveTemp(Next);return true;
}

bool FAetherProfile::CollectPending()
{
 if(PendingGold==0&&PendingMaterial==0)return false;
 auto Next=*this;if(Next.Gold>10000000-PendingGold||(PendingMaterial>0&&!Next.Add("Material",PendingMaterial)))return false;
 Next.Gold+=PendingGold;Next.PendingGold=0;Next.PendingMaterial=0;*this=MoveTemp(Next);return true;
}

bool FAetherProfile::TryAutoClaim(FName Id){const auto* Q=FAetherRules::Get().Quest(Id);return Q&&Q->bAutoClaim&&Claim(Id);}
