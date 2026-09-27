#include "Inventory/AetherInventoryState.h"
FAetherInventoryMutation FAetherInventoryStateV10::TransferExact(FAetherInventoryStateV10& Destination,FGuid Id,int32 Quantity,
    int32 DestinationIndex,FGuid ExpectedTarget,EAetherTransferMode Mode,const FAetherV10ItemDefinitions& Definitions)
{
    using E=EAetherInventoryMutationCode;FString Reason;
    if(this==&Destination||DestinationIndex<0||DestinationIndex>=Destination.Capacity||
        (Mode!=EAetherTransferMode::PlaceOrMerge&&Mode!=EAetherTransferMode::SwapWhole))return {E::Invalid};
    auto SourceDefs=Definitions,TargetDefs=Definitions;
    SourceDefs.DefaultCapacity=Capacity;TargetDefs.DefaultCapacity=Destination.Capacity;
    if(!Validate(SourceDefs,Reason)||!Destination.Validate(TargetDefs,Reason))return {E::Invalid};
    for(const auto& Item:Items)if(Destination.Find(Item.InstanceId))return {E::Invalid};
    const auto* Source=Find(Id);const auto* Target=Destination.At(DestinationIndex);
    if(!Source)return {E::Missing};
    if(Quantity<1||Quantity>Source->Quantity)return {E::Invalid};
    if((Target?Target->InstanceId:FGuid())!=ExpectedTarget)return {E::Occupied};
    if(IsEquipped(Id)||(Target&&Destination.IsEquipped(Target->InstanceId)))return {E::Equipped};
    auto From=*this,To=Destination;const auto SourceItem=*Source;
    FAetherInventoryMutation Result;Result.Code=E::Applied;Result.ActualQuantity=Quantity;Result.AffectedIds.Add(Id);
    if(Mode==EAetherTransferMode::SwapWhole)
    {
        if(!Target||Quantity!=SourceItem.Quantity)return {E::Invalid};
        auto Reverse=*Target;auto Moved=SourceItem;
        Reverse.SlotIndex=SourceItem.SlotIndex;Moved.SlotIndex=DestinationIndex;
        From.Items.RemoveAll([&](const auto& V){return V.InstanceId==Id;});
        To.Items.RemoveAll([&](const auto& V){return V.InstanceId==ExpectedTarget;});
        From.Items.Add(Reverse);To.Items.Add(Moved);
        Result.AffectedIds.Add(ExpectedTarget);Result.Transitions={{Id,Id,Quantity},{ExpectedTarget,ExpectedTarget,Reverse.Quantity}};
    }
    else
    {
        FGuid ResultId=Id;
        if(Target)
        {
            if(!SourceItem.SameStackKey(*Target))return {E::Incompatible};
            if(Definitions.Items.FindChecked(SourceItem.DefinitionId).MaxStack-Target->Quantity<Quantity)return {E::Capacity};
            auto* Merged=To.Items.FindByPredicate([&](const auto& V){return V.InstanceId==ExpectedTarget;});
            Merged->Quantity+=Quantity;ResultId=ExpectedTarget;
        }
        else
        {
            auto Moved=SourceItem;Moved.Quantity=Quantity;Moved.SlotIndex=DestinationIndex;
            if(Quantity<SourceItem.Quantity)do {Moved.InstanceId=FGuid::NewGuid();}while(From.Find(Moved.InstanceId)||To.Find(Moved.InstanceId));
            ResultId=Moved.InstanceId;To.Items.Add(Moved);
        }
        auto* Remaining=From.Items.FindByPredicate([&](const auto& V){return V.InstanceId==Id;});Remaining->Quantity-=Quantity;
        if(Remaining->Quantity==0)From.Items.RemoveAll([&](const auto& V){return V.InstanceId==Id;});
        Result.AffectedIds.AddUnique(ResultId);Result.Transitions.Add({Id,ResultId,Quantity});
    }
    if(!From.Validate(SourceDefs,Reason)||!To.Validate(TargetDefs,Reason))return {E::Invalid};
    *this=MoveTemp(From);Destination=MoveTemp(To);return Result;
}
FAetherInventoryMutation FAetherInventoryStateV10::TransferTo(FAetherInventoryStateV10& Destination,FGuid Id,int32 Quantity,
    bool AllowPartial,const FAetherV10ItemDefinitions& Definitions)
{
    using E=EAetherInventoryMutationCode;FString Reason;
    if(this==&Destination||!Definitions.Validate(Reason))return {E::Invalid};
    // 背包固定容量由 Profile 复验；普通容器的容量由自身配置复验。本值规则只接受 1–256 的合法格网。
    auto SourceDefinitions=Definitions,TargetDefinitions=Definitions;
    SourceDefinitions.DefaultCapacity=Capacity;TargetDefinitions.DefaultCapacity=Destination.Capacity;
    if(!Validate(SourceDefinitions,Reason)||!Destination.Validate(TargetDefinitions,Reason))return {E::Invalid};
    for(const auto& I:Items)if(Destination.Find(I.InstanceId))return {E::Invalid};
    const auto* Found=Find(Id);if(!Found)return {E::Missing};
    if(IsEquipped(Id))return {E::Equipped};
    if(Quantity<1||Quantity>Found->Quantity)return {E::Invalid};
    const auto SourceItem=*Found;const auto& Def=Definitions.Items.FindChecked(SourceItem.DefinitionId);
    auto From=*this,To=Destination;int32 Remaining=Quantity;
    FAetherInventoryMutation Result;Result.Code=E::Applied;Result.AffectedIds.Add(Id);
    // 普通整件移动优先保持原身份，不为了自动整理强制吸收到另一堆。
    if(Quantity==SourceItem.Quantity&&To.FirstEmpty()!=INDEX_NONE)
    {
        auto Moved=SourceItem;Moved.SlotIndex=To.FirstEmpty();To.Items.Add(Moved);Remaining=0;
        Result.Transitions.Add({Id,Id,Quantity});
    }
    else
    {
        for(int32 Cell=0;Cell<To.Capacity&&Remaining>0;++Cell)
        {
            auto* Target=To.Items.FindByPredicate([&](const auto& I){return I.SlotIndex==Cell;});
            if(!Target||To.IsEquipped(Target->InstanceId)||!SourceItem.SameStackKey(*Target))continue;
            const int32 Moved=FMath::Min(Remaining,Def.MaxStack-Target->Quantity);
            if(Moved>0)
            {
                Target->Quantity+=Moved;Remaining-=Moved;Result.AffectedIds.AddUnique(Target->InstanceId);
                Result.Transitions.Add({Id,Target->InstanceId,Moved});
            }
        }
        if(Remaining>0&&To.FirstEmpty()!=INDEX_NONE)
        {
            auto Moved=SourceItem;Moved.Quantity=Remaining;Moved.SlotIndex=To.FirstEmpty();
            // 此路径由部分拆分进入；复制全部实例状态后只分配新身份/数量/位置。
            do {Moved.InstanceId=FGuid::NewGuid();} while(From.Find(Moved.InstanceId)||To.Find(Moved.InstanceId));
            To.Items.Add(Moved);Result.AffectedIds.Add(Moved.InstanceId);
            Result.Transitions.Add({Id,Moved.InstanceId,Remaining});Remaining=0;
        }
    }
    const int32 Actual=Quantity-Remaining;
    if(Actual==0||(!AllowPartial&&Remaining>0))return {E::Capacity};
    auto* Original=From.Items.FindByPredicate([&](const auto& I){return I.InstanceId==Id;});Original->Quantity-=Actual;
    if(Original->Quantity==0)From.Items.RemoveAll([&](const auto& I){return I.InstanceId==Id;});
    if(!From.Validate(SourceDefinitions,Reason)||!To.Validate(TargetDefinitions,Reason))return {E::Invalid};
    // 唯一发布点：两个值对象同时更换；SQL 持久原子性由上层同一个事务保障。
    Result.ActualQuantity=Actual;*this=MoveTemp(From);Destination=MoveTemp(To);return Result;
}
