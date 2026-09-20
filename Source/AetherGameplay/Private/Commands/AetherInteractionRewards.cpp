#include "AetherInteractionRewards.h"
#include "World/AetherWorldState.h"
#include "Quests/AetherQuestProgression.h"
namespace
{
using R=EAetherCommandCode;
R Grant(FAetherProfileStateV10& P,const TMap<FName,int32>& Items,int32 Gold,const FAetherV10ItemDefinitions& D)
{
    if(int64(P.Gold)+Gold>10000000)return R::Capacity;
    for(const auto& Item:Items)
    {
        const auto Added=P.Inventory.AddNew(Item.Key.ToString(),Item.Value,D);
        if(Added.Code==EAetherInventoryMutationCode::Capacity)return R::Capacity;
        if(Added.Code!=EAetherInventoryMutationCode::Applied)return R::Invalid;
    }
    P.Gold+=Gold;return R::Applied;
}
bool Consume(FAetherProfileStateV10& P,const TMap<FName,int32>& Ingredients)
{
    // 配方授权可消耗被定义为任务材料的补给；仍保护玩家锁定、装备与其他人的绑定实例。
    // 固定按持久格子顺序消耗，UI 过滤顺序不影响哪一堆被使用。
    P.Inventory.Items.Sort([](const auto& A,const auto& B){return A.SlotIndex<B.SlotIndex;});
    for(const auto& Ingredient:Ingredients)
    {
        int32 Left=Ingredient.Value;
        for(auto& Item:P.Inventory.Items)if(Left>0&&Item.DefinitionId.Equals(Ingredient.Key.ToString(),ESearchCase::CaseSensitive)&&
            !Item.bLocked&&!Item.QuestInstanceId.IsValid()&&!P.Inventory.IsEquipped(Item.InstanceId)&&
            (Item.BoundToCharacter.IsEmpty()||Item.BoundToCharacter.Equals(P.CharacterId,ESearchCase::CaseSensitive)))
        {const int32 Taken=FMath::Min(Left,Item.Quantity);Item.Quantity-=Taken;Left-=Taken;}
        if(Left>0)return false;
    }
    P.Inventory.Items.RemoveAll([](const auto& I){return I.Quantity==0;});return true;
}
}
EAetherCommandCode AetherInteractionRewards::Apply(const FAetherInteractionActionDefinition& A,const FAetherProfileCommandContext& X,
    FAetherProfileStateV10& P,FAetherWorldStateV10& W,const FAetherV10ItemDefinitions& Items,const FAetherRules& Rules)
{
    using K=EAetherInteractionActionKind;
    if(!X.bServiceRequirementsMet)return R::NotReady;
    if(A.Kind==K::CollectSupply)
    {
        if(P.Evidence.Contains(A.ObjectiveId)||!AetherQuestProgression::Observe(P,A.ObjectiveId,Rules))return R::NotAllowed;
        const auto* Table=Rules.LootTables.Find(FName(*A.ServiceId));return Table?Grant(P,*Table,0,Items):R::Invalid;
    }
    if(A.Kind==K::ObserveObjective)
    {
        if(!X.bObjectiveFactReady)return R::NotReady;
        const auto* Rule=Rules.Objectives.Find(FName(*A.ObjectiveId));if(!Rule)return R::Invalid;
        if(Rule->Scope==EAetherObjectiveScope::World)
        {
            bool Source=false;for(FName Id:Rule->FactSources)Source|=Id.ToString().Equals(X.Interaction.TargetStableId,ESearchCase::CaseSensitive);
            if(!Source)return R::Unauthorized;
            if(!W.WorldFactSources.Contains(A.ObjectiveId))W.WorldFactSources.Add(A.ObjectiveId,X.Interaction.TargetStableId);
        }
        if(P.Evidence.Contains(A.ObjectiveId))return R::NotAllowed;
        return AetherQuestProgression::Observe(P,A.ObjectiveId,Rules)?R::Applied:R::NotAllowed;
    }
    if(X.ServerUnixMs<=0||X.ServerUnixMs>253402297199999LL)return R::NotReady;
    const FString Day=FDateTime::FromUnixTimestamp(X.ServerUnixMs/1000).ToString(TEXT("%Y%m%d"));
    if(P.DailyDate>Day)return R::NotReady;
    if(P.DailyDate!=Day){P.DailyDate=Day;P.DailyEvidence.Reset();P.DailyClaims.Reset();}
    if(A.Kind==K::RecordDaily)
    {
        if(!X.bObjectiveFactReady)return R::NotReady;
        bool Allowed=false;for(const auto& Daily:Rules.Dailies)if(P.Claims.Contains(Daily.QuestGate.ToString()))
            for(FName Fact:Daily.Facts)Allowed|=Fact.ToString().Equals(A.ObjectiveId,ESearchCase::CaseSensitive);
        if(!Allowed||P.DailyEvidence.Contains(A.ObjectiveId))return R::NotAllowed;
        P.DailyEvidence.Add(A.ObjectiveId);return R::Applied;
    }
    if(A.Kind==K::CollectGather)
    {
        bool Known=false;for(FName Id:Rules.DailyGatherSources)Known|=Id.ToString().Equals(X.Interaction.TargetStableId,ESearchCase::CaseSensitive);
        if(!Known)return R::Unauthorized;
        if(P.DailyEvidence.Contains(X.Interaction.TargetStableId))return R::NotAllowed;
        const auto* Table=Rules.LootTables.Find(FName(*A.ServiceId));if(!Table)return R::Invalid;
        const auto Result=Grant(P,*Table,0,Items);if(Result!=R::Applied)return Result;
        P.DailyEvidence.Add(X.Interaction.TargetStableId);return R::Applied;
    }
    if(A.Kind==K::ClaimDaily)
    {
        const auto* Daily=Rules.Dailies.FindByPredicate([&](const auto& V){return V.Id.ToString().Equals(A.ServiceId,ESearchCase::CaseSensitive);});
        if(!Daily||!P.Claims.Contains(Daily->QuestGate.ToString())||P.DailyClaims.Contains(Daily->Id.ToString()))return R::NotAllowed;
        for(FName Fact:Daily->Facts)if(!P.DailyEvidence.Contains(Fact.ToString()))return R::NotReady;
        if(!Consume(P,Daily->Consume))return R::NotAllowed;
        const auto Result=Grant(P,Daily->Reward,Daily->Gold,Items);if(Result!=R::Applied)return Result;
        P.DailyClaims.Add(Daily->Id.ToString());return R::Applied;
    }
    return R::UnsupportedAction;
}
