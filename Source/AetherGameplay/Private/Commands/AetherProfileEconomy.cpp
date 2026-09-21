#include "AetherProfileEconomy.h"
namespace
{
EAetherCommandCode Mutation(FAetherInventoryMutation M,FAetherCommandResult& R)
{
    using E=EAetherInventoryMutationCode;using C=EAetherCommandCode;
    if(M.Code==E::Applied){R.ActualQuantity=M.ActualQuantity;for(const auto& Id:M.AffectedIds)R.AffectedIds.AddUnique(Id);return C::Applied;}
    if(M.Code==E::Capacity)return C::Capacity;if(M.Code==E::Missing)return C::Missing;if(M.Code==E::Invalid)return C::Invalid;return C::NotAllowed;
}
}
EAetherCommandCode AetherProfileEconomy::Apply(const FAetherPlayerCommand& C,FAetherProfileStateV10& P,
    const FAetherProfileCommandContext& Context,const FAetherV10ItemDefinitions& Items,
    const FAetherEconomyDefinitionsV10& Economy,FAetherCommandResult& R)
{
    using E=EAetherCommandType;using Code=EAetherCommandCode;
    if(!Context.bCanManageInventory)return Code::NotReady;
    if(C.Type==E::ClaimReward)
    {
        // 协议 v1 的有限 DefinitionId 载荷承载奖励记录键 Reward.<32 位 GUID>，不是奖励数量或来源声明。
        FGuid RewardId;
        if(!C.DefinitionId.StartsWith(TEXT("Reward."),ESearchCase::CaseSensitive)||
            !FGuid::ParseExact(C.DefinitionId.Mid(7),EGuidFormats::Digits,RewardId)||!RewardId.IsValid())return Code::Invalid;
        const auto* Found=P.PendingRewards.FindByPredicate([&](const auto& V){return V.RewardId==RewardId;});
        if(!Found)return P.ClaimedRewardIds.Contains(RewardId)?Code::NotAllowed:Code::Missing;
        if(P.ClaimedRewardIds.Num()>=4096||int64(P.Gold)+Found->Gold>10000000)return Code::Capacity;
        const auto Reward=*Found;TArray<FString> Keys;Reward.Items.GetKeys(Keys);Keys.Sort();
        int32 Total=0;auto Remaining=Reward;
        for(const auto& Key:Keys)
        {
            // 以实际剩余格数和完整 StackKey 试算最大可领取量；未容纳部分仍属于同一 RewardId。
            int32 Low=0,High=Reward.Items[Key];
            while(Low<High)
            {
                const int32 Count=Low+(High-Low+1)/2;auto Candidate=P.Inventory;
                const auto Trial=Candidate.AddNew(Key,Count,Items);
                if(Trial.Code==EAetherInventoryMutationCode::Applied)Low=Count;
                else if(Trial.Code==EAetherInventoryMutationCode::Capacity)High=Count-1;
                else return Code::Invalid;
            }
            if(Low>0)
            {
                const auto Result=Mutation(P.Inventory.AddNew(Key,Low,Items),R);if(Result!=Code::Applied)return Result;
                Total+=Low;Remaining.Items[Key]-=Low;if(Remaining.Items[Key]==0)Remaining.Items.Remove(Key);
            }
        }
        if(!Total&&!Reward.Gold)return Code::Capacity;
        P.Gold+=Reward.Gold;Remaining.Gold=0;
        if(Remaining.Items.IsEmpty())
        {P.PendingRewards.RemoveAll([&](const auto& V){return V.RewardId==RewardId;});P.ClaimedRewardIds.Add(RewardId);}
        else *P.PendingRewards.FindByPredicate([&](const auto& V){return V.RewardId==RewardId;})=MoveTemp(Remaining);
        R.ActualQuantity=Total;R.AffectedDefinitionIds.Add(C.DefinitionId);R.ReasonParameters.Add(TEXT("GoldChanged"),FString::FromInt(Reward.Gold));
        return Code::Applied;
    }
    // 此上下文由当前服务器目标/会话解析；旧 UI 缓存价格、距离或另一个商人的清单不能授权。
    if(!Context.bTradeSessionValid||!Context.TradeTargetStableId.Equals(C.TargetStableId,ESearchCase::CaseSensitive))return Code::OutOfReach;
    FString Reason;if(!Economy.Validate(Items,Reason))return Code::NotReady;
    const auto* Shop=Economy.Shops.Find(Context.ShopId);if(!Shop||!Shop->Id.Equals(Context.ShopId,ESearchCase::CaseSensitive))return Code::NotAllowed;
    if(C.Type==E::BuyItem)
    {
        const auto* D=Items.Items.Find(C.DefinitionId);
        if(!D||!D->Id.Equals(C.DefinitionId,ESearchCase::CaseSensitive)||!Shop->Products.Contains(C.DefinitionId)||D->BuyPrice<=0)return Code::NotAllowed;
        const int64 Cost=int64(D->BuyPrice)*C.Quantity;if(Cost>P.Gold)return Code::InsufficientFunds;
        const auto Result=Mutation(P.Inventory.AddNew(D->Id,C.Quantity,Items),R);if(Result!=Code::Applied)return Result;
        P.Gold-=int32(Cost);R.ReasonParameters.Add(TEXT("GoldChanged"),FString::Printf(TEXT("%lld"),-Cost));return Code::Applied;
    }
    const auto* Found=P.Inventory.Find(C.ItemInstanceId);if(!Found)return Code::Missing;
    const auto Item=*Found;const auto* D=Items.Items.Find(Item.DefinitionId);if(!D)return Code::Invalid;
    if(C.Type==E::SellItem)
    {
        if(!Shop->AcceptedCategories.Contains(D->Category))return Code::NotAllowed;
        const int64 Price=int64(D->SellPrice)*C.Quantity;if(int64(P.Gold)+Price>10000000)return Code::Capacity;
        const auto Result=Mutation(P.Inventory.RemoveForSale(Item.InstanceId,C.Quantity,P.CharacterId,Items),R);
        if(Result!=Code::Applied)return Result;
        P.Gold+=int32(Price);R.ReasonParameters.Add(TEXT("GoldChanged"),FString::Printf(TEXT("%lld"),Price));return Code::Applied;
    }
    if(C.Type==E::RepairItem)
    {
        if(!Shop->bRepair||D->MaxDurability<=0||Item.Durability>=D->MaxDurability||
            (!Item.BoundToCharacter.IsEmpty()&&!Item.BoundToCharacter.Equals(P.CharacterId,ESearchCase::CaseSensitive)))return Code::NotAllowed;
        const int64 Cost=int64(D->MaxDurability-Item.Durability)*Shop->RepairGoldPerPoint;
        if(Cost>P.Gold)return Code::InsufficientFunds;
        const auto Result=Mutation(P.Inventory.Repair(Item.InstanceId,Items),R);if(Result!=Code::Applied)return Result;
        P.Gold-=int32(Cost);R.ReasonParameters.Add(TEXT("GoldChanged"),FString::Printf(TEXT("%lld"),-Cost));return Code::Applied;
    }
    return Code::UnsupportedAction;
}
