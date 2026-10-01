#include "AetherServerReward.h"
#include "Quests/AetherQuestProgression.h"
namespace
{
bool Grant(FAetherProfileStateV10& P,const TMap<FString,int32>& Items,int32 Gold,const FString& Source,
    const FAetherV10ItemDefinitions& Definitions,bool Defer,FString& Reason)
{
    auto Candidate=P;bool Fits=int64(P.Gold)+Gold<=10000000;
    TArray<FString> Keys;Items.GetKeys(Keys);Keys.Sort();
    for(const auto& Id:Keys)if(Fits)
    {
        const auto R=Candidate.Inventory.AddNew(Id,Items[Id],Definitions);
        if(R.Code==EAetherInventoryMutationCode::Capacity)Fits=false;
        else if(R.Code!=EAetherInventoryMutationCode::Applied){Reason=TEXT("Invalid reward item");return false;}
    }
    if(Fits){Candidate.Gold+=Gold;P=MoveTemp(Candidate);return true;}
    if(!Defer||P.PendingRewards.Num()>=128){Reason=TEXT("Reward capacity unavailable");return false;}
    // 外层实例回执/每日领取标记与此整份待领奖同一事务提交，失败不会拆开发一半。
    FAetherPendingRewardV10 Reward;Reward.RewardId=FGuid::NewGuid();Reward.SourceId=Source;Reward.Items=Items;Reward.Gold=Gold;
    P.PendingRewards.Add(MoveTemp(Reward));return true;
}
}
EAetherLootClaimOutcome AetherServerRewards::ApplyLoot(const FAetherServerFact& E,FAetherProfileStateV10& P,
    FAetherWorldStateV10& W,const FAetherV10Definitions& D,FString& Reason)
{
    using O=EAetherLootClaimOutcome;
    if(E.Kind!=EAetherServerFactKind::LegacyLoot||!E.InstanceId.IsValid())return O::Invalid;
    auto* Loot=W.Loot.FindByPredicate([&](const auto& L){return L.ClaimId==E.InstanceId;});
    if(!Loot){Reason=TEXT("Loot no longer exists");return O::Missing;}
    if(!Loot->ClaimedBy.IsEmpty())
        return Loot->ClaimedBy.Equals(P.CharacterId,ESearchCase::CaseSensitive)?O::AlreadyOwned:O::ClaimedByOther;
    auto Items=Loot->Items;if(Items.IsEmpty())Items.Add(Loot->Definition,Loot->Count);
    // 整包预检在值副本上完成；容量失败不改库存，也不消耗共享掉落所有权。
    auto Candidate=P;TArray<FString> Keys;Items.GetKeys(Keys);Keys.Sort();
    for(const auto& Id:Keys)
    {
        const auto R=Candidate.Inventory.AddNew(Id,Items[Id],D.Items);
        if(R.Code==EAetherInventoryMutationCode::Capacity){Reason=TEXT("Loot inventory capacity unavailable");return O::InventoryFull;}
        if(R.Code!=EAetherInventoryMutationCode::Applied){Reason=TEXT("Invalid loot item");return O::Invalid;}
    }
    P=MoveTemp(Candidate);Loot->ClaimedBy=P.CharacterId;Reason.Reset();return O::Applied;
}
bool AetherServerRewards::Apply(const FAetherServerFact& E,FAetherProfileStateV10& P,FAetherWorldStateV10& W,
    const FAetherV10Definitions& D,FString& Reason)
{
    if(E.Kind!=EAetherServerFactKind::EncounterReward)return false;
    const auto* Rule=D.Rules.ActivityRewards.Find(FName(*E.FactId));if(!Rule){Reason=TEXT("Encounter reward definition missing");return false;}
    auto& Run=E.FactId==TEXT("Abbey")?W.Abbey:W.Relay;
    if(!Run.Instance.IsValid()||Run.Instance!=E.InstanceId||Run.Phase!=5||!Run.Participants.Contains(P.CharacterId))
    {Reason=TEXT("Encounter success checkpoint is not committed for this participant");return false;}
    if(Run.Settled.Contains(P.CharacterId))return true;
    auto& Receipt=E.FactId==TEXT("Abbey")?P.LastAbbeyReceipt:P.LastRelayReceipt;
    if(Receipt!=E.InstanceId)
    {
        // UTC 日历单调前进。旧进程已接受但延后完成的奖励不会回退日历或再占新的一天。
        if(P.DailyDate<E.UtcDay){P.DailyDate=E.UtcDay;P.DailyEvidence.Reset();P.DailyClaims.Reset();}
        const FString Claim=Rule->DailyClaim.ToString();
        if(P.DailyDate==E.UtcDay&&!P.DailyClaims.Contains(Claim))
        {
            TMap<FString,int32> Items;for(const auto& Item:Rule->Items)Items.Add(Item.Key.ToString(),Item.Value);
            if(!Grant(P,Items,Rule->Gold,TEXT("Encounter.")+E.FactId+TEXT(".")+E.UtcDay,D.Items,true,Reason))return false;
            P.DailyClaims.Add(Claim);
        }
        if(!Rule->Objective.IsNone()&&!P.Evidence.Contains(Rule->Objective.ToString()))
            AetherQuestProgression::Observe(P,Rule->Objective.ToString(),D.Rules);
        Receipt=E.InstanceId;
    }
    Run.Settled.AddUnique(P.CharacterId);return true;
}
