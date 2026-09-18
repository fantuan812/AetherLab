#include "Commands/AetherProfileCommand.h"
#include "Profile/AetherProfileCodec.h"
#include "AetherProfileEconomy.h"
#include "AetherProfileConsumable.h"
namespace
{
EAetherCommandCode InventoryCode(EAetherInventoryMutationCode C)
{
    using E=EAetherInventoryMutationCode;using R=EAetherCommandCode;
    switch(C){case E::Applied:return R::Applied;case E::Missing:return R::Missing;case E::Capacity:return R::Capacity;
    case E::Invalid:return R::Invalid;default:return R::NotAllowed;}
}
EAetherCommandCode SkillCode(EAetherSkillMutationCode C)
{
    using E=EAetherSkillMutationCode;using R=EAetherCommandCode;
    switch(C){case E::Applied:case E::Unchanged:return R::Applied;case E::Missing:return R::Missing;case E::Invalid:return R::Invalid;
    case E::Conflict:return R::Conflict;case E::Capacity:return R::Capacity;case E::InsufficientPoints:return R::InsufficientFunds;
    case E::NotReady:return R::NotReady;default:return R::NotAllowed;}
}
}
bool AetherProfileCommands::Prepare(const FAetherPlayerCommand& C,const FString& Actor,
    const FAetherProfileStateV10& Current,const FAetherProfileCommandContext& Context,
    const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,
    FAetherTransaction& Transaction,FAetherCommandResult& Result,const FAetherEconomyDefinitionsV10& Economy)
{
    Result={};Result.CommandId=C.CommandId;Result.FinalProfileRevision=Current.Revision;FString Reason;
    const auto Fail=[&](EAetherCommandCode Code)
    {
        Result.Code=Code;Result.ActualQuantity=0;Result.AffectedIds.Reset();Result.AffectedDefinitionIds.Reset();Result.Transfers.Reset();
        Result.ReasonParameters.Reset();Result.FinalProfileRevision=Current.Revision;Result.FinalWorldRevision=-1;return false;
    };
    if(C.ProtocolVersion!=AetherCommands::ProtocolVersion)return Fail(EAetherCommandCode::UnsupportedProtocol);
    if(!AetherCommands::Validate(C,Reason))return Fail(EAetherCommandCode::Invalid);
    if(!Actor.Equals(Current.CharacterId,ESearchCase::CaseSensitive))return Fail(EAetherCommandCode::Unauthorized);
    if(!Current.Validate(Items,Skills,Rules,Reason))return Fail(EAetherCommandCode::StorageUnavailable);
    if(C.ExpectedProfileRevision!=Current.Revision)return Fail(EAetherCommandCode::StaleRevision);
    if(Current.Revision>=MAX_int64-1)return Fail(EAetherCommandCode::NotReady);
    // 永远在副本上调用领域规则；失败路径不会清空已准备的事务，也不会改调用者的状态。
    auto Next=Current;FAetherInventoryMutation Inventory;FAetherSkillMutation Skill;
    TOptional<FAetherEffectDelivery> Effect;
    bool IsInventory=true,IsSkill=false;
    using E=EAetherCommandType;
    switch(C.Type)
    {
    case E::MoveItem:case E::SwapItems:case E::SplitStack:case E::MergeStack:case E::SetItemLock:
    case E::SetItemFavorite:case E::SortInventory:case E::EquipItem:case E::UnequipItem:
        if(!Context.bCanManageInventory)return Fail(EAetherCommandCode::NotReady);
        break;
    default:IsInventory=false;break;
    }
    switch(C.Type)
    {
    case E::MoveItem:Inventory=Next.Inventory.Move(C.ItemInstanceId,C.DestinationIndex,Items);break;
    case E::SwapItems:Inventory=Next.Inventory.Swap(C.ItemInstanceId,C.OtherInstanceId,Items);break;
    case E::SplitStack:Inventory=Next.Inventory.Split(C.ItemInstanceId,C.Quantity,FGuid::NewGuid(),C.DestinationIndex,Items);break;
    case E::MergeStack:Inventory=Next.Inventory.Merge(C.ItemInstanceId,C.OtherInstanceId,C.Quantity,Items);break;
    case E::SetItemLock:Inventory=Next.Inventory.SetLocked(C.ItemInstanceId,C.Enabled,Items);break;
    case E::SetItemFavorite:Inventory=Next.Inventory.SetFavorite(C.ItemInstanceId,C.Enabled,Items);break;
    case E::SortInventory:Inventory=Next.Inventory.Sort(C.Enabled,Items);break;
    case E::EquipItem:Inventory=Next.Inventory.Equip(C.ItemInstanceId,C.SlotId,Actor,Items);break;
    case E::UnequipItem:Inventory=Next.Inventory.Unequip(C.ItemInstanceId,Items);break;
    case E::LearnSkill:case E::UpgradeSkill:case E::ResetSkills:case E::BindSkill:
    {
        IsSkill=true;auto SkillContext=Context.Skill;
        // 完成任务始终来自已提交角色，忽略上下文中可能过期的任务集合。
        SkillContext.CompletedQuests.Reset();for(const auto& Id:Current.Claims)SkillContext.CompletedQuests.Add(Id);
        if(C.Type==E::LearnSkill||C.Type==E::UpgradeSkill)
        {
            const int32 Rank=Current.Skills.PermanentRank(C.SkillId);
            if((C.Type==E::LearnSkill&&Rank!=0)||(C.Type==E::UpgradeSkill&&Rank==0))return Fail(EAetherCommandCode::NotAllowed);
            Skill=Next.Skills.LearnNext(C.SkillId,C.CommandId,SkillContext,Skills);
        }
        else if(C.Type==E::ResetSkills)Skill=Next.Skills.Reset({},SkillContext,Skills,Context.ExternalSkillGrants);
        else
        {
            int32 Slot=INDEX_NONE;
            for(int32 I=0;I<FAetherSkillStateV10::HotbarCapacity;++I)
                if(C.SlotId==FString::Printf(TEXT("Hotbar.%d"),I+1))Slot=I;
            if(Slot==INDEX_NONE)return Fail(EAetherCommandCode::Invalid);
            Skill=Next.Skills.Bind(Slot,C.SkillId,Context.ExternalSkillGrants,Skills);
        }
        break;
    }
    case E::UseItem:
    {
        FAetherEffectDelivery Delivery;
        Result.Code=AetherProfileConsumable::Apply(C,Next,Context,Items,Rules,Delivery,Result);
        if(Result.Code==EAetherCommandCode::Applied)Effect=MoveTemp(Delivery);
        break;
    }
    case E::BuyItem:case E::SellItem:case E::RepairItem:case E::ClaimReward:
        Result.Code=AetherProfileEconomy::Apply(C,Next,Context,Items,Economy,Result);
        break;
    // 掉落/容器由跨域处理器处理；交互尚未实现，不能按成功空操作提交。
    default:return Fail(EAetherCommandCode::UnsupportedAction);
    }
    if(IsInventory)
    {
        Result.Code=InventoryCode(Inventory.Code);Result.ActualQuantity=Inventory.ActualQuantity;
        Result.AffectedIds=MoveTemp(Inventory.AffectedIds);
        for(const auto& T:Inventory.Transitions){Result.Transfers.Add({T.From,T.To,T.Quantity});Result.AffectedIds.AddUnique(T.From);Result.AffectedIds.AddUnique(T.To);}
    }
    if(IsSkill)
    {
        Result.Code=SkillCode(Skill.Code);Result.AffectedDefinitionIds=MoveTemp(Skill.AffectedSkills);
        Result.ReasonParameters.Add(TEXT("SkillPointsChanged"),FString::FromInt(Skill.PointsChanged));
    }
    if(Result.Code!=EAetherCommandCode::Applied)return Fail(Result.Code);
    ++Next.Revision;FAetherTransaction Candidate;
    Candidate.ActorId=Actor;Candidate.CommandId=C.CommandId;Candidate.ExpectedProfileRevision=Current.Revision;
    Candidate.ProtocolVersion=C.ProtocolVersion;Result.FinalProfileRevision=Next.Revision;
    FAetherAggregateWrite Write;Write.ExpectedRevision=Current.Revision;
    Write.Value.Key={EAetherAggregateKind::Profile,Actor};Write.Value.Revision=Next.Revision;
    if(!AetherCommands::Encode(C,Candidate.Request,Reason)||!AetherCommands::EncodeResult(Result,Candidate.Result,Reason)||
        !AetherProfileCodec::Encode(Next,Items,Skills,Rules,Write.Value.Payload,Reason))return Fail(EAetherCommandCode::Invalid);
    Candidate.Writes.Add(MoveTemp(Write));
    if(Effect.IsSet())Candidate.Effects.Add(MoveTemp(Effect.GetValue()));
    if(!AetherTransactions::Validate(Candidate,Reason))return Fail(EAetherCommandCode::Invalid);
    Transaction=MoveTemp(Candidate);return true;
}
