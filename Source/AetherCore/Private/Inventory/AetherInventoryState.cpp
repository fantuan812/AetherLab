#include "Inventory/AetherInventoryState.h"
namespace
{
using E=EAetherInventoryMutationCode;
FAetherInventoryMutation Fail(E Code){FAetherInventoryMutation R;R.Code=Code;return R;}
FAetherInventoryMutation Applied(FGuid Id,int32 Quantity=1)
{FAetherInventoryMutation R;R.Code=E::Applied;R.ActualQuantity=Quantity;R.AffectedIds.Add(Id);return R;}
FAetherV10ItemInstance* Mutable(FAetherInventoryStateV10& State,FGuid Id)
{return State.Items.FindByPredicate([&](const auto& I){return I.InstanceId==Id;});}
}
bool FAetherV10ItemInstance::SameStackKey(const FAetherV10ItemInstance& O) const
{
    // FString / TMap 的默认相等会折叠大小写。实例状态必须逐项精确比较，不能把
    // Wet 与 wet、不同绑定字符或不同词条键合并后只保留其中一份元数据。
    if(!DefinitionId.Equals(O.DefinitionId,ESearchCase::CaseSensitive)||Quality!=O.Quality||Durability!=O.Durability||
        !BoundToCharacter.Equals(O.BoundToCharacter,ESearchCase::CaseSensitive)||!StateGroup.Equals(O.StateGroup,ESearchCase::CaseSensitive)||
        QuestInstanceId!=O.QuestInstanceId||bLocked!=O.bLocked||bFavorite!=O.bFavorite||Affixes.Num()!=O.Affixes.Num())return false;
    for(const auto& A:Affixes)
    {
        bool Found=false;for(const auto& B:O.Affixes)if(A.Key.Equals(B.Key,ESearchCase::CaseSensitive)&&A.Value==B.Value){Found=true;break;}
        if(!Found)return false;
    }
    return true;
}
const FAetherV10ItemInstance* FAetherInventoryStateV10::Find(FGuid Id) const
{return Items.FindByPredicate([&](const auto& I){return I.InstanceId==Id;});}
const FAetherV10ItemInstance* FAetherInventoryStateV10::At(int32 Slot) const
{return Items.FindByPredicate([&](const auto& I){return I.SlotIndex==Slot;});}
int32 FAetherInventoryStateV10::FirstEmpty() const
{for(int32 I=0;I<Capacity;++I)if(!At(I))return I;return INDEX_NONE;}
bool FAetherInventoryStateV10::IsEquipped(FGuid Id) const
{return Equipment.FindKey(Id)!=nullptr;}

// 先验证定义，再验证实例，再验证引用。脏状态不能通过一次移动被悄悄“修好”并覆盖原档。
bool FAetherInventoryStateV10::Validate(const FAetherV10ItemDefinitions& D,FString& Reason) const
{
    if(!D.Validate(Reason))return false;
    const auto Reject=[&](const TCHAR* Text){Reason=Text;return false;};
    if(Capacity!=D.DefaultCapacity||Items.Num()>Capacity||Equipment.Num()>D.Slots.Num())
        return Reject(TEXT("Invalid capacity or equipment bounds"));
    TSet<FGuid> Ids;TSet<int32> Positions;
    for(const auto& I:Items)
    {
        const auto* Def=D.Items.Find(I.DefinitionId);
        if(!I.InstanceId.IsValid()||Ids.Contains(I.InstanceId)||I.SlotIndex<0||I.SlotIndex>=Capacity||
            Positions.Contains(I.SlotIndex)||!Def||!Def->Id.Equals(I.DefinitionId,ESearchCase::CaseSensitive)||I.Quantity<1||I.Quantity>Def->MaxStack)
            return Reject(TEXT("Invalid instance identity, cell or quantity"));
        if(I.Quality<0||I.Quality>5||I.Affixes.Num()>16||I.BoundToCharacter.Len()>128||I.StateGroup.Len()>96)
            return Reject(TEXT("Invalid instance metadata"));
        for(const auto& A:I.Affixes)if(A.Key.IsEmpty()||A.Key.Len()>96||A.Value<0||A.Value>1000000)
            return Reject(TEXT("Invalid affix state"));
        if(Def->MaxDurability==0 ? I.Durability!=-1 : (I.Durability<0||I.Durability>Def->MaxDurability))
            return Reject(TEXT("Invalid durability state"));
        Ids.Add(I.InstanceId);Positions.Add(I.SlotIndex);
    }
    TSet<FGuid> EquippedIds;TSet<FString> Occupied;
    for(const auto& Pair:Equipment)
    {
        const auto* I=Find(Pair.Value);const auto* Slot=D.FindSlot(Pair.Key);
        const auto* Def=I?D.Items.Find(I->DefinitionId):nullptr;
        if(!I||!Def||!Slot||I->Quantity!=1||EquippedIds.Contains(I->InstanceId)||
            !Def->AllowedSlots.Contains(Pair.Key)||!Slot->AllowedCategories.Contains(Def->Category)||Occupied.Contains(Pair.Key))
            return Reject(TEXT("Invalid equipment reference or layout"));
        EquippedIds.Add(I->InstanceId);Occupied.Add(Pair.Key);
        for(const auto& Extra:Def->AdditionalOccupiedSlots)
        {
            if(Extra==Pair.Key||Occupied.Contains(Extra)||!D.FindSlot(Extra))return Reject(TEXT("Conflicting occupied slots"));
            Occupied.Add(Extra);
        }
    }
    Reason.Reset();return true;
}
FAetherInventoryMutation FAetherInventoryStateV10::Publish(FAetherInventoryStateV10&& Candidate,FAetherInventoryMutation Result,const FAetherV10ItemDefinitions& D)
{
    FString Reason;if(!Candidate.Validate(D,Reason))return Fail(E::Invalid);
    *this=MoveTemp(Candidate);return Result;
}
FAetherInventoryMutation FAetherInventoryStateV10::Move(FGuid Id,int32 Destination,const FAetherV10ItemDefinitions& D)
{
    FString InitialError;if(!Validate(D,InitialError))return Fail(E::Invalid);
    if(!Find(Id))return Fail(E::Missing);
    if(Destination<0||Destination>=Capacity)return Fail(E::Invalid);
    if(const auto* Occupant=At(Destination);Occupant&&Occupant->InstanceId!=Id)return Fail(E::Occupied);
    auto Next=*this;Mutable(Next,Id)->SlotIndex=Destination;
    return Publish(MoveTemp(Next),Applied(Id),D);
}
FAetherInventoryMutation FAetherInventoryStateV10::Swap(FGuid A,FGuid B,const FAetherV10ItemDefinitions& D)
{
    FString InitialError;if(!Validate(D,InitialError))return Fail(E::Invalid);
    if(A==B)return Fail(E::Invalid);
    if(!Find(A)||!Find(B))return Fail(E::Missing);
    auto Next=*this;const int32 Cell=Mutable(Next,A)->SlotIndex;
    Mutable(Next,A)->SlotIndex=Mutable(Next,B)->SlotIndex;Mutable(Next,B)->SlotIndex=Cell;
    auto R=Applied(A);R.AffectedIds.Add(B);return Publish(MoveTemp(Next),MoveTemp(R),D);
}
// 拆分复制完整实例状态，只改新堆 GUID/数量/格子；绑定、锁定、词条和任务归属不得丢失。
FAetherInventoryMutation FAetherInventoryStateV10::Split(FGuid Id,int32 Quantity,FGuid NewId,int32 Destination,const FAetherV10ItemDefinitions& D)
{
    FString InitialError;if(!Validate(D,InitialError))return Fail(E::Invalid);
    const auto* Source=Find(Id);if(!Source)return Fail(E::Missing);
    if(Quantity<1||Quantity>=Source->Quantity||!NewId.IsValid()||Find(NewId))return Fail(E::Invalid);
    if(IsEquipped(Id))return Fail(E::Equipped);
    if(Destination==-1)Destination=FirstEmpty();
    if(Destination<0)return Fail(E::Capacity);
    if(Destination>=Capacity)return Fail(E::Invalid);
    if(At(Destination))return Fail(E::Occupied);
    auto Next=*this;auto New=*Source;New.InstanceId=NewId;New.Quantity=Quantity;New.SlotIndex=Destination;
    Mutable(Next,Id)->Quantity-=Quantity;Next.Items.Add(MoveTemp(New));
    auto R=Applied(Id,Quantity);R.AffectedIds.Add(NewId);R.Transitions.Add({Id,NewId,Quantity});
    return Publish(MoveTemp(Next),MoveTemp(R),D);
}
// 合并以目标实例为保留身份；回执记录实际转移量，源有剩余时保留其原始 GUID。
FAetherInventoryMutation FAetherInventoryStateV10::Merge(FGuid From,FGuid To,int32 Quantity,const FAetherV10ItemDefinitions& D)
{
    FString InitialError;if(!Validate(D,InitialError))return Fail(E::Invalid);
    const auto* Source=Find(From);const auto* Target=Find(To);
    if(!Source||!Target)return Fail(E::Missing);
    if(From==To||Quantity<1||Quantity>Source->Quantity)return Fail(E::Invalid);
    if(IsEquipped(From)||IsEquipped(To))return Fail(E::Equipped);
    const auto* Def=D.Items.Find(Source->DefinitionId);
    if(!Def||Def->MaxStack<=1||!Source->SameStackKey(*Target))return Fail(E::Incompatible);
    const int32 Actual=FMath::Min(Quantity,Def->MaxStack-Target->Quantity);
    if(Actual<=0)return Fail(E::Capacity);
    auto Next=*this;Mutable(Next,From)->Quantity-=Actual;Mutable(Next,To)->Quantity+=Actual;
    Next.Items.RemoveAll([](const auto& I){return I.Quantity==0;});
    auto R=Applied(From,Actual);R.AffectedIds.Add(To);R.Transitions.Add({From,To,Actual});
    return Publish(MoveTemp(Next),MoveTemp(R),D);
}
FAetherInventoryMutation FAetherInventoryStateV10::SetLocked(FGuid Id,bool Enabled,const FAetherV10ItemDefinitions& D)
{
    FString InitialError;if(!Validate(D,InitialError))return Fail(E::Invalid);
    if(!Find(Id))return Fail(E::Missing);
    auto Next=*this;Mutable(Next,Id)->bLocked=Enabled;
    return Publish(MoveTemp(Next),Applied(Id),D);
}
FAetherInventoryMutation FAetherInventoryStateV10::SetFavorite(FGuid Id,bool Enabled,const FAetherV10ItemDefinitions& D)
{
    FString InitialError;if(!Validate(D,InitialError))return Fail(E::Invalid);
    if(!Find(Id))return Fail(E::Missing);
    auto Next=*this;Mutable(Next,Id)->bFavorite=Enabled;
    return Publish(MoveTemp(Next),Applied(Id),D);
}
FAetherInventoryMutation FAetherInventoryStateV10::Sort(bool MergeStacks,const FAetherV10ItemDefinitions& D)
{
    FString Reason;if(!Validate(D,Reason))return Fail(E::Invalid);
    auto Next=*this;auto R=Fail(E::Applied);
    Next.Items.Sort([](const auto& A,const auto& B)
    {
        const int32 Name=A.DefinitionId.Compare(B.DefinitionId,ESearchCase::CaseSensitive);
        if(Name!=0)return Name<0;
        if(A.Quality!=B.Quality)return A.Quality>B.Quality;
        return A.InstanceId.ToString(EGuidFormats::Digits)<B.InstanceId.ToString(EGuidFormats::Digits);
    });
    if(MergeStacks)
    {
        // 按稳定 ID 次序选择保留堆，不能让数组遍历顺序决定最后保留谁的身份。
        for(int32 To=0;To<Next.Items.Num();++To)for(int32 From=To+1;From<Next.Items.Num();++From)
        {
            auto& Target=Next.Items[To];auto& Source=Next.Items[From];
            const auto* Def=D.Items.Find(Target.DefinitionId);
            if(!Def||Def->MaxStack<=1||Target.Quantity==0||Source.Quantity==0||Next.IsEquipped(Target.InstanceId)||
                Next.IsEquipped(Source.InstanceId)||!Target.SameStackKey(Source))continue;
            const int32 Actual=FMath::Min(Source.Quantity,Def->MaxStack-Target.Quantity);
            if(Actual>0){Target.Quantity+=Actual;Source.Quantity-=Actual;R.Transitions.Add({Source.InstanceId,Target.InstanceId,Actual});}
        }
        Next.Items.RemoveAll([](const auto& I){return I.Quantity==0;});
    }
    for(int32 I=0;I<Next.Items.Num();++I){Next.Items[I].SlotIndex=I;R.AffectedIds.Add(Next.Items[I].InstanceId);}
    return Publish(MoveTemp(Next),MoveTemp(R),D);
}
// 此层只计算合法 loadout。数据库提交成功后，角色适配器才可更新 ASC 与外观。
FAetherInventoryMutation FAetherInventoryStateV10::Equip(FGuid Id,const FString& SlotId,const FString& Actor,const FAetherV10ItemDefinitions& D)
{
    FString InitialError;if(!Validate(D,InitialError))return Fail(E::Invalid);
    const auto* I=Find(Id);const auto* Slot=D.FindSlot(SlotId);const auto* Def=I?D.Items.Find(I->DefinitionId):nullptr;
    if(!I)return Fail(E::Missing);
    if(!Def||!Slot||I->Quantity!=1||!Def->AllowedSlots.Contains(SlotId)||!Slot->AllowedCategories.Contains(Def->Category))
        return Fail(E::NotAllowed);
    if(!I->BoundToCharacter.IsEmpty()&&!I->BoundToCharacter.Equals(Actor,ESearchCase::CaseSensitive))return Fail(E::Bound);
    for(const auto& Pair:Equipment)
    {
        const auto* Existing=Find(Pair.Value);const auto* Other=Existing?D.Items.Find(Existing->DefinitionId):nullptr;
        // 向已被双手武器占用的副手装备盾必须拒绝；不隐式卸下主手。
        if(Pair.Value!=Id&&Other&&Other->AdditionalOccupiedSlots.Contains(SlotId))return Fail(E::Occupied);
    }
    auto Next=*this;auto R=Applied(Id);
    for(auto It=Next.Equipment.CreateIterator();It;++It)
        if(It.Value()==Id||It.Key()==SlotId||Def->AdditionalOccupiedSlots.Contains(It.Key()))
        {R.AffectedIds.AddUnique(It.Value());It.RemoveCurrent();}
    Next.Equipment.Add(SlotId,Id);
    return Publish(MoveTemp(Next),MoveTemp(R),D);
}
// 卸装只解除引用，即使所有背包格都占满也能执行；物品始终在原格子里。
FAetherInventoryMutation FAetherInventoryStateV10::Unequip(FGuid Id,const FAetherV10ItemDefinitions& D)
{
    FString InitialError;if(!Validate(D,InitialError))return Fail(E::Invalid);
    if(!IsEquipped(Id))return Fail(E::Missing);
    auto Next=*this;for(auto It=Next.Equipment.CreateIterator();It;++It)if(It.Value()==Id)It.RemoveCurrent();
    return Publish(MoveTemp(Next),Applied(Id),D);
}
FAetherInventoryMutation FAetherInventoryStateV10::Wear(FGuid Id,int32 Amount,const FAetherV10ItemDefinitions& D)
{
    FString InitialError;if(!Validate(D,InitialError))return Fail(E::Invalid);
    const auto* I=Find(Id);if(!I)return Fail(E::Missing);
    if(Amount<1||I->Durability<=0)return Fail(E::NotAllowed);
    auto Next=*this;Mutable(Next,Id)->Durability=FMath::Max(0,I->Durability-Amount);
    return Publish(MoveTemp(Next),Applied(Id),D);
}
FAetherInventoryMutation FAetherInventoryStateV10::Repair(FGuid Id,const FAetherV10ItemDefinitions& D)
{
    FString InitialError;if(!Validate(D,InitialError))return Fail(E::Invalid);
    const auto* I=Find(Id);const auto* Def=I?D.Items.Find(I->DefinitionId):nullptr;
    if(!I)return Fail(E::Missing);
    if(!Def||Def->MaxDurability<=0||I->Durability>=Def->MaxDurability)return Fail(E::NotAllowed);
    auto Next=*this;Mutable(Next,Id)->Durability=Def->MaxDurability;
    return Publish(MoveTemp(Next),Applied(Id),D);
}
EAetherInventoryMutationCode FAetherInventoryStateV10::CanRemove(FGuid Id,const FString& Actor,bool ForSale,const FAetherV10ItemDefinitions& D) const
{
    FString Reason;if(!Validate(D,Reason)||Actor.IsEmpty())return E::Invalid;
    const auto* I=Find(Id);const auto* Def=I?D.Items.Find(I->DefinitionId):nullptr;
    if(!I)return E::Missing;
    if(!Def)return E::Invalid;
    if(IsEquipped(Id))return E::Equipped;
    if(I->bLocked||Def->bQuestLocked||I->QuestInstanceId.IsValid())return E::Locked;
    // 绑定物不能借由出售/丢弃脱离绑定；持有人本身也不能绕过。
    if(!I->BoundToCharacter.IsEmpty())return E::Bound;
    if(ForSale ? !Def->bSellable : !Def->bDroppable)return E::NotAllowed;
    return E::Applied;
}
// 每次从已确认 loadout 重建总附加值，不在旧结果上累加，避免重生/重连叠加效果。
TMap<FString,double> FAetherInventoryStateV10::EquippedStats(const FAetherV10ItemDefinitions& D) const
{
    TMap<FString,double> Result;FString Reason;if(!Validate(D,Reason))return Result;
    for(const auto& Pair:Equipment)
    {
        const auto* I=Find(Pair.Value);const auto& Def=D.Items.FindChecked(I->DefinitionId);
        const double Scale=Def.MaxDurability>0&&I->Durability==0?Def.BrokenStatMultiplier:1.;
        for(const auto& Stat:Def.Stats)Result.FindOrAdd(Stat.Key)+=Stat.Value*Scale;
    }
    return Result;
}
