#include "Inventory/AetherInventoryState.h"
FAetherInventoryMutation FAetherInventoryStateV10::AddNew(const FString& DefinitionId,int32 Quantity,const FAetherV10ItemDefinitions& D)
{
    using E=EAetherInventoryMutationCode;FString Reason;
    if(!Validate(D,Reason)||Quantity<1||Quantity>1000000)return {E::Invalid};
    const auto* Def=D.Items.Find(DefinitionId);if(!Def||!Def->Id.Equals(DefinitionId,ESearchCase::CaseSensitive))return {E::Missing};
    FAetherV10ItemInstance Default;Default.DefinitionId=DefinitionId;Default.Durability=Def->MaxDurability>0?Def->MaxDurability:-1;
    auto Next=*this;int32 Remaining=Quantity;FAetherInventoryMutation Result;Result.Code=E::Applied;
    // 按真实格子填充，绝不把绑定/锁定/磨损/词条不同的同名物品洗成默认状态。
    for(int32 Cell=0;Cell<Capacity&&Remaining>0;++Cell)
    {
        auto* I=Next.Items.FindByPredicate([&](const auto& V){return V.SlotIndex==Cell;});
        if(!I||Next.IsEquipped(I->InstanceId)||!I->SameStackKey(Default))continue;
        const int32 Added=FMath::Min(Remaining,Def->MaxStack-I->Quantity);
        if(Added>0){I->Quantity+=Added;Remaining-=Added;Result.AffectedIds.Add(I->InstanceId);}
    }
    while(Remaining>0)
    {
        const int32 Cell=Next.FirstEmpty();if(Cell==INDEX_NONE)return {E::Capacity};
        auto I=Default;I.InstanceId=FGuid::NewGuid();I.SlotIndex=Cell;I.Quantity=FMath::Min(Remaining,Def->MaxStack);
        Remaining-=I.Quantity;Result.AffectedIds.Add(I.InstanceId);Next.Items.Add(MoveTemp(I));
    }
    Result.ActualQuantity=Quantity;return Publish(MoveTemp(Next),MoveTemp(Result),D);
}
FAetherInventoryMutation FAetherInventoryStateV10::RemoveForSale(FGuid Id,int32 Quantity,const FString& Actor,const FAetherV10ItemDefinitions& D)
{
    using E=EAetherInventoryMutationCode;const auto Allowed=CanRemove(Id,Actor,true,D);
    if(Allowed!=E::Applied)return {Allowed};
    const auto* Item=Find(Id);if(Quantity<1||Quantity>Item->Quantity)return {E::Invalid};
    auto Next=*this;auto* Mutable=Next.Items.FindByPredicate([&](const auto& I){return I.InstanceId==Id;});
    Mutable->Quantity-=Quantity;if(Mutable->Quantity==0)Next.Items.RemoveAll([&](const auto& I){return I.InstanceId==Id;});
    FAetherInventoryMutation Result;Result.Code=E::Applied;Result.ActualQuantity=Quantity;Result.AffectedIds.Add(Id);
    return Publish(MoveTemp(Next),MoveTemp(Result),D);
}
