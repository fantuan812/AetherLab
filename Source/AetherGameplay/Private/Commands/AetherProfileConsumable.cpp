#include "AetherProfileConsumable.h"
EAetherCommandCode AetherProfileConsumable::Apply(const FAetherPlayerCommand& C,FAetherProfileStateV10& Next,
    const FAetherProfileCommandContext& Context,const FAetherV10ItemDefinitions& Items,const FAetherRules& Rules,
    FAetherEffectDelivery& Delivery,FAetherCommandResult& Result)
{
    using R=EAetherCommandCode;
    const auto& Before=Context.Resources;
    if(!Context.bCanManageInventory||Context.ResourceReservationId!=C.CommandId||!Before.Validate()||
        Before.Health<=0||Before.Revision>=MAX_int64-1||Context.ServerUnixMs<=0||Context.ServerUnixMs>253402297199999LL||
        !FMath::IsFinite(Context.SafeForSeconds)||Context.SafeForSeconds<0)return R::NotReady;
    if(Context.ServerUnixMs<Before.UseReadyAtUnixMs)return R::NotReady;
    const auto* I=Next.Inventory.Find(C.ItemInstanceId);if(!I)return R::Missing;
    const auto* Def=Items.Items.Find(I->DefinitionId);if(!Def||Def->UseId.IsEmpty())return R::NotAllowed;
    const auto* Use=Rules.Uses.Find(FName(*Def->UseId));if(!Use)return R::NotAllowed;
    // 锁定与任务实例不允许消费；绑定给本人的普通消耗品可用，收藏只影响展示。
    if(I->bLocked||I->QuestInstanceId.IsValid()||Def->bQuestLocked||Next.Inventory.IsEquipped(I->InstanceId)||
        (!I->BoundToCharacter.IsEmpty()&&!I->BoundToCharacter.Equals(Next.CharacterId,ESearchCase::CaseSensitive)))return R::NotAllowed;
    const auto Amount=[](double V){return FMath::IsFinite(V)&&V>=0&&V<=1000;};
    if(!Amount(Use->Health)||!Amount(Use->Mana)||!Amount(Use->Stamina)||!FMath::IsFinite(Use->Cooldown)||
        Use->Cooldown<0||Use->Cooldown>3600||!FMath::IsFinite(Use->SafeSeconds)||Use->SafeSeconds<0||Use->SafeSeconds>3600)return R::Invalid;
    if(Context.SafeForSeconds<Use->SafeSeconds)return R::NotReady;
    FAetherConsumableEffectV10 Effect;Effect.DeliveryId=C.CommandId;Effect.ItemInstanceId=I->InstanceId;
    Effect.DefinitionId=I->DefinitionId;Effect.ProfileRevision=Next.Revision+1;Effect.Before=Before;Effect.After=Before;
    auto& After=Effect.After;++After.Revision;
    After.Health=FMath::Min(After.MaxHealth,After.Health+Use->Health);
    After.Mana=FMath::Min(After.MaxMana,After.Mana+Use->Mana);
    After.Stamina=FMath::Min(After.MaxStamina,After.Stamina+Use->Stamina);
    // 满资源不会白白扣药；只要本物品实际恢复一种资源即可使用。
    if(After.Health==Before.Health&&After.Mana==Before.Mana&&After.Stamina==Before.Stamina)return R::NotAllowed;
    After.UseReadyAtUnixMs=Context.ServerUnixMs+FMath::CeilToInt64(Use->Cooldown*1000);
    FAetherEffectDelivery Candidate;Candidate.Id=C.CommandId;Candidate.ActorId=Next.CharacterId;
    if(!AetherConsumableEffects::Encode(Effect,Candidate.Payload))return R::Invalid;
    // 保存效果成功后才改候选库存；两者随后进入同一个 SQLite 事务，真实库存仍未发布。
    auto* Mutable=Next.Inventory.Items.FindByPredicate([&](const auto& V){return V.InstanceId==C.ItemInstanceId;});
    --Mutable->Quantity;
    if(Mutable->Quantity==0)Next.Inventory.Items.RemoveAll([&](const auto& V){return V.InstanceId==C.ItemInstanceId;});
    Result.ActualQuantity=1;Result.AffectedIds.Add(C.ItemInstanceId);Result.AffectedDefinitionIds.Add(Effect.DefinitionId);
    Result.ReasonParameters.Add(TEXT("EffectDeliveryId"),C.CommandId.ToString(EGuidFormats::Digits));
    Delivery=MoveTemp(Candidate);return R::Applied;
}
