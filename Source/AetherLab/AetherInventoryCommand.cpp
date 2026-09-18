#include "AetherInventoryCommand.h"
#include "AetherProfile.h"
bool FAetherInventoryCommand::SameRequest(const FAetherInventoryCommand& O) const
{
 return CommandId==O.CommandId&&ExpectedInventoryRevision==O.ExpectedInventoryRevision&&Action==O.Action
 &&ItemInstanceId==O.ItemInstanceId&&DestinationInstanceId==O.DestinationInstanceId&&Quantity==O.Quantity&&DefinitionId==O.DefinitionId&&ShopId==O.ShopId;
}
bool AetherItems::Grant(FAetherProfile& P,const TMap<FName,int32>& Items,int32 Gold,const FAetherRules& R)
{
 if(Gold<0||Gold>10000000||P.Gold>10000000-Gold)return false;
 auto Next=P;for(const auto& I:Items)if(!Next.Add(I.Key,I.Value,R))return false;
 Next.Gold+=Gold;P=MoveTemp(Next);return true;
}
bool AetherItems::GrantTable(FAetherProfile& P,FName Table,const FAetherRules& R)
{const auto* Items=R.LootTables.Find(Table);return Items&&Grant(P,*Items,0,R);}
const FAetherUseRule* AetherItems::Use(const FAetherProfile& P,FGuid Instance,const FAetherRules& R)
{
 const auto* Item=P.Inventory.FindByPredicate([&](const auto& S){return S.InstanceId==Instance;});
 const auto* Rule=Item?R.Items.Find(Item->DefinitionId):nullptr;return Rule?R.Uses.Find(Rule->UseId):nullptr;
}
EAetherInventoryResult AetherItems::Prepare(FAetherProfile& P,const FAetherInventoryCommand& C,const FAetherRules& R,int32& Moved)
{
 using E=EAetherInventoryResult;Moved=0;
 if(!C.CommandId.IsValid()||C.Quantity<1||C.Quantity>1000||C.ExpectedInventoryRevision<0)return E::InvalidCommand;
 if(C.ExpectedInventoryRevision!=P.Revision)return E::StaleRevision;
 auto Next=P;auto* Item=Next.Inventory.FindByPredicate([&](const auto& S){return S.InstanceId==C.ItemInstanceId;});
 if(C.Action=="Buy")
 {
  const auto* Shop=R.Shops.Find(C.ShopId);const auto* Rule=R.Items.Find(C.DefinitionId);
  if(!Shop||!Shop->Contains(C.DefinitionId)||!Rule||Rule->Buy<=0)return E::NotAllowed;
  const int64 Cost=int64(Rule->Buy)*C.Quantity;if(Cost>Next.Gold)return E::InsufficientFunds;
  if(!Next.Add(C.DefinitionId,C.Quantity,R))return E::Capacity;Next.Gold-=int32(Cost);Moved=C.Quantity;
 }
 else
 {
  if(!Item)return E::MissingInstance;const auto* Rule=R.Items.Find(Item->DefinitionId);if(!Rule)return E::NotAllowed;
  if(C.Action=="Equip"){if(C.Quantity!=1||!Next.Equip(C.ItemInstanceId,R))return E::NotAllowed;Moved=1;}
  else if(C.Action=="Unequip"){const auto* Slot=Next.Equipped.FindKey(C.ItemInstanceId);if(!Slot||C.Quantity!=1)return E::NotAllowed;FName Key=*Slot;Next.Equipped.Remove(Key);Moved=1;}
  else if(C.Action=="Split"){if(Next.Inventory.Num()>=R.InventoryCapacity)return E::Capacity;if(!Next.Split(C.ItemInstanceId,C.Quantity,R))return E::NotAllowed;Moved=C.Quantity;}
  else if(C.Action=="Merge")
  {
   auto* To=Next.Inventory.FindByPredicate([&](const auto& S){return S.InstanceId==C.DestinationInstanceId;});if(!To)return E::MissingInstance;
   if(To==Item||To->DefinitionId!=Item->DefinitionId||Next.Equipped.FindKey(Item->InstanceId)||Next.Equipped.FindKey(To->InstanceId)||C.Quantity>Item->Count)return E::NotAllowed;
   Moved=FMath::Min(C.Quantity,Rule->MaxStack-To->Count);if(Moved<=0)return E::Capacity;
   Item->Count-=Moved;To->Count+=Moved;Next.Inventory.RemoveAll([](const auto& S){return S.Count==0;});
  }
  else if(C.Action=="Use"||C.Action=="Sell")
  {
   if(!Rule->bRemovable||Next.Equipped.FindKey(Item->InstanceId)||Item->Count<C.Quantity)return E::NotAllowed;
   if(C.Action=="Use"){if(C.Quantity!=1||!Use(Next,C.ItemInstanceId,R))return E::NotAllowed;}
   else{const int64 Gold=int64(Rule->Sell)*C.Quantity;if(!R.Shops.Contains(C.ShopId)||!Rule->bSellable||Rule->Sell<=0||Gold>10000000-Next.Gold)return E::NotAllowed;Next.Gold+=int32(Gold);}
   Item->Count-=C.Quantity;Moved=C.Quantity;Next.Inventory.RemoveAll([](const auto& S){return S.Count==0;});
  }
  else return E::InvalidCommand;
 }
 if(!Next.Validate(R))return E::NotAllowed;P=MoveTemp(Next);return E::Applied;
}
