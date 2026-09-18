#include "AetherProfile.h"
#include "AetherInventoryRules.h"
int32 FAetherInventoryData::MaxStack(FName Id, const FAetherRules& Rules)
{const auto* R=Rules.Items.Find(Id);return R?R->MaxStack:0;}
int32 FAetherInventoryData::Count(FName Id) const
{ int32 N=0; for (const auto& S:Inventory) if (S.DefinitionId==Id) N+=S.Count; return N; }
bool FAetherInventoryData::Add(FName Id, int32 Q, const FAetherRules& Rules)
{
    const int32 Limit=MaxStack(Id,Rules); if (Limit==0 || Q<=0 || Q>1000) return false;
    auto Next=Inventory;
    for (auto& S:Next) if (S.DefinitionId==Id) { const int32 N=FMath::Min(Q,Limit-S.Count); S.Count+=N; Q-=N; }
    while (Q>0) { if (Next.Num()>=Rules.InventoryCapacity) return false; FAetherItemStack S; S.InstanceId=FGuid::NewGuid(); S.DefinitionId=Id; S.Count=FMath::Min(Q,Limit); Q-=S.Count; Next.Add(S); }
    Inventory=MoveTemp(Next); return true;
}
bool FAetherInventoryData::Remove(FName Id, int32 Q, const FAetherRules& Rules)
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
bool FAetherInventoryData::Split(FGuid Id, int32 Q, const FAetherRules& Rules)
{
    if (Inventory.Num()>=Rules.InventoryCapacity || Q<=0) return false;
    auto* S=Inventory.FindByPredicate([Id](const auto& V){return V.InstanceId==Id;});
    if (!S || Q>=S->Count || Equipped.FindKey(Id)) return false;
    FAetherItemStack N=*S; N.InstanceId=FGuid::NewGuid(); N.Count=Q; S->Count-=Q; Inventory.Add(N); return true;
}
bool FAetherInventoryData::Merge(FGuid From, FGuid To, const FAetherRules& Rules)
{
    if (From==To || Equipped.FindKey(From) || Equipped.FindKey(To)) return false;
    auto* A=Inventory.FindByPredicate([From](const auto& S){return S.InstanceId==From;});
    auto* B=Inventory.FindByPredicate([To](const auto& S){return S.InstanceId==To;});
    if (!A || !B || A->DefinitionId!=B->DefinitionId) return false;
    const int32 N=FMath::Min(A->Count,MaxStack(A->DefinitionId,Rules)-B->Count); if (N<=0) return false;
    A->Count-=N; B->Count+=N; Inventory.RemoveAll([](const auto& S){return S.Count==0;}); return true;
}
bool FAetherInventoryData::Equip(FGuid Id,const FAetherRules& R){return AetherInventory::Equip(*this,Id,R);}
