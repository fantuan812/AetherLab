#include "Inspection/AetherInspectionService.h"

namespace
{
void Field(FAetherInspectionModel& M,const TCHAR* Key,const TCHAR* Label,FString Value)
{M.Fields.Add({Key,Label,MoveTemp(Value)});}
void Action(FAetherInspectionModel& M,EAetherInspectAction Kind,FString Argument,const TCHAR* Label,
    bool Enabled,FString Reason={},int32 Max=1,bool Confirm=false)
{M.Actions.Add({Kind,MoveTemp(Argument),Label,MoveTemp(Reason),Max,Enabled,Confirm});}
void Invalid(FAetherInspectionModel& M,const TCHAR* Message)
{M.State=EAetherInspectionState::Invalid;M.Message=Message;M.Actions.Reset();}
void DefinitionFields(FAetherInspectionModel& M,const FAetherV10ItemDefinition& D)
{
    M.Title=D.DisplayName;M.IconId=D.IconId;M.Category=D.Category;
    Field(M,TEXT("category"),TEXT("类别"),D.Category);
    Field(M,TEXT("use"),TEXT("用途"),D.UseId.IsEmpty()?TEXT("不可直接使用"):D.UseId);
    if(!D.UseId.IsEmpty())
    {
        // 原生定义尚未提供动态使用快照，不能把未知的冷却显示为零或启用未经校验的使用按钮。
        Field(M,TEXT("cooldown"),TEXT("使用冷却"),TEXT("暂不可用"));
        Field(M,TEXT("restriction"),TEXT("使用限制"),TEXT("需要满足当前生命、资源和动作条件"));
    }
    if(!D.AllowedSlots.IsEmpty())Field(M,TEXT("slots"),TEXT("可装备槽"),FString::Join(D.AllowedSlots,TEXT(" / ")));
    if(!D.AdditionalOccupiedSlots.IsEmpty())Field(M,TEXT("occupied"),TEXT("额外占用"),FString::Join(D.AdditionalOccupiedSlots,TEXT(" / ")));
    Field(M,TEXT("source"),TEXT("来源"),TEXT("暂无来源记录"));
}
void Compare(FAetherInspectionModel& M,const FAetherInspectionSnapshot& S,const FAetherV10ItemDefinitions& D)
{
    const auto& Target=M.Request.Target;
    if(Target.ComparisonSlot.IsEmpty())return;
    if(!M.ComparisonSlots.Contains(Target.ComparisonSlot))
    {M.ComparisonMessage=TEXT("请选择该装备允许的替换槽。");return;}
    // 完全复用原生 Equip 的双手冲突/绑定/替换规则，再复用战斗适配器的 EquippedStats。
    // 候选仅在函数栈上存活，绝不发布到真实库存，也不会写角色装备组件。
    auto Candidate=S.Inventory;
    const auto Result=Candidate.Equip(Target.InstanceId,Target.ComparisonSlot,S.Context.OwnerIdentity,D);
    if(Result.Code!=EAetherInventoryMutationCode::Applied)
    {M.ComparisonMessage=AetherInspection::InventoryReason(Result.Code);return;}
    const auto Before=S.Inventory.EquippedStats(D),After=Candidate.EquippedStats(D);
    TSet<FString> Keys;for(const auto& P:Before)Keys.Add(P.Key);for(const auto& P:After)Keys.Add(P.Key);
    TArray<FString> Ordered=Keys.Array();Ordered.Sort();
    for(const auto& Key:Ordered)M.Comparison.Add({Key,Before.FindRef(Key),After.FindRef(Key)});
    // 双手替换的副手也必须列出，而非只比较目标格子的单件数据。
    for(const auto& P:S.Inventory.Equipment)
        if(!Candidate.IsEquipped(P.Value))M.DisplacedInstances.AddUnique(P.Value);
    M.DisplacedInstances.Sort([](const FGuid& A,const FGuid& B){return A.ToString()<B.ToString();});
    M.ComparisonMessage=TEXT("比较装备附加总值；正负差异包含耐久折减和所有被卸下的装备。");
}
void Item(FAetherInspectionModel& M,const FAetherInspectionSnapshot& S,const FAetherV10ItemDefinitions& D)
{
    const auto& T=M.Request.Target;FString Reason;
    if(!S.Inventory.Validate(D,Reason)){Invalid(M,TEXT("库存快照或物品定义无效。"));return;}
    if(T.Kind==EAetherInspectTarget::EquipmentSlot)
    {
        if(!D.FindSlot(T.SlotId)){Invalid(M,TEXT("装备槽不存在。"));return;}
        if(S.Inventory.Equipment.FindRef(T.SlotId)!=T.InstanceId)
        {M.State=EAetherInspectionState::Changed;M.Message=TEXT("装备槽中的对象已变化，请重新查看。");return;}
        if(!T.InstanceId.IsValid())
        {M.Title=T.SlotId;M.Category=TEXT("EquipmentSlot");M.Message=TEXT("空装备槽");return;}
    }
    const auto* I=S.Inventory.Find(T.InstanceId);
    if(!I){M.State=EAetherInspectionState::Changed;M.Message=TEXT("对象已变化或已移出库存，请重新选择。");return;}
    if(!T.DefinitionId.IsEmpty()&&!T.DefinitionId.Equals(I->DefinitionId,ESearchCase::CaseSensitive))
    {M.State=EAetherInspectionState::Changed;M.Message=TEXT("对象定义已变化，请重新选择。");return;}
    const auto& Def=D.Items.FindChecked(I->DefinitionId);DefinitionFields(M,Def);M.Item=*I;
    Field(M,TEXT("quantity"),TEXT("数量"),FString::FromInt(I->Quantity));
    Field(M,TEXT("quality"),TEXT("品质"),FString::FromInt(I->Quality));
    Field(M,TEXT("locked"),TEXT("锁定"),I->bLocked?TEXT("已锁定"):TEXT("未锁定"));
    Field(M,TEXT("favorite"),TEXT("收藏"),I->bFavorite?TEXT("已收藏"):TEXT("未收藏"));
    Field(M,TEXT("binding"),TEXT("绑定"),I->BoundToCharacter.IsEmpty()?TEXT("未绑定"):I->BoundToCharacter);
    if(Def.MaxDurability>0)
        Field(M,TEXT("durability"),TEXT("耐久"),FString::Printf(TEXT("%d / %d"),I->Durability,Def.MaxDurability));
    else Field(M,TEXT("durability"),TEXT("耐久"),TEXT("不适用"));
    const auto Sell=S.Inventory.CanRemove(I->InstanceId,S.Context.OwnerIdentity,true,D);
    Field(M,TEXT("trade"),TEXT("交易状态"),Sell==EAetherInventoryMutationCode::Applied?TEXT("允许出售；仍需有效商人报价"):AetherInspection::InventoryReason(Sell));
    M.ComparisonSlots=Def.AllowedSlots;
    for(const auto& Slot:Def.AllowedSlots)
    {
        auto Candidate=S.Inventory;const auto R=Candidate.Equip(I->InstanceId,Slot,S.Context.OwnerIdentity,D);
        Action(M,EAetherInspectAction::Equip,Slot,TEXT("装备"),R.Code==EAetherInventoryMutationCode::Applied,AetherInspection::InventoryReason(R.Code));
    }
    if(S.Inventory.IsEquipped(I->InstanceId))Action(M,EAetherInspectAction::Unequip,{},TEXT("卸下"),true);
    const auto Drop=S.Inventory.CanRemove(I->InstanceId,S.Context.OwnerIdentity,false,D);
    Action(M,EAetherInspectAction::Drop,{},TEXT("丢弃"),Drop==EAetherInventoryMutationCode::Applied&&S.WorldRevision>=0,
        S.WorldRevision<0?TEXT("当前暂不可丢弃"):AetherInspection::InventoryReason(Drop),I->Quantity,true);
    Action(M,I->bLocked?EAetherInspectAction::Unlock:EAetherInspectAction::Lock,{},I->bLocked?TEXT("解锁"):TEXT("锁定"),true);
    Action(M,I->bFavorite?EAetherInspectAction::Unfavorite:EAetherInspectAction::Favorite,{},I->bFavorite?TEXT("取消收藏"):TEXT("收藏"),true);
    if(!Def.UseId.IsEmpty())Action(M,EAetherInspectAction::Use,{},TEXT("使用"),S.bCanAct&&!I->bLocked&&!S.Inventory.IsEquipped(I->InstanceId),TEXT("需满足资源、动作和服务器冷却条件"));
    if(I->Quantity>1)Action(M,EAetherInspectAction::Split,{},TEXT("拆分"),S.Inventory.FirstEmpty()!=INDEX_NONE&&!I->bLocked&&!S.Inventory.IsEquipped(I->InstanceId),
        TEXT("需要空格且物品未锁定、未装备"),I->Quantity-1,true);
    if(S.Shop.IsSet()&&!S.TradeTargetStableId.IsEmpty())
    {
        const auto& Shop=S.Shop.GetValue();
        Field(M,TEXT("sellPrice"),TEXT("单件售价"),FString::FromInt(Def.SellPrice));
        Action(M,EAetherInspectAction::Sell,S.TradeTargetStableId,TEXT("出售"),
            S.bCanAct&&Sell==EAetherInventoryMutationCode::Applied&&Shop.AcceptedCategories.Contains(Def.Category),AetherInspection::InventoryReason(Sell),I->Quantity,true);
        M.Actions.Last().UnitPrice=Def.SellPrice;
        if(Shop.bRepair&&Def.MaxDurability>0)
        {
            const int64 Price=int64(Def.MaxDurability-I->Durability)*Shop.RepairGoldPerPoint;
            Field(M,TEXT("repairPrice"),TEXT("完整修理价格"),LexToString(Price));
            auto Candidate=S.Inventory;const auto Repair=Candidate.Repair(I->InstanceId,D);
            Action(M,EAetherInspectAction::Repair,S.TradeTargetStableId,TEXT("修理"),S.bCanAct&&Price>0&&Price<=S.Gold&&Repair.Code==EAetherInventoryMutationCode::Applied,
                Price>S.Gold?TEXT("金币不足"):AetherInspection::InventoryReason(Repair.Code),1,true);
            M.Actions.Last().UnitPrice=Price;
        }
    }
    Compare(M,S,D);
}
void Skill(FAetherInspectionModel& M,const FAetherInspectionSnapshot& S,const FAetherSkillDefinitionsV10& D)
{
    FString Reason;if(!S.Skills.Validate(D,Reason)||!FAetherSkillStateV10::ValidateExternalGrants(S.ExternalGrants,D))
    {Invalid(M,TEXT("技能快照或定义无效。"));return;}
    const auto& T=M.Request.Target;const auto* Def=D.Skills.Find(T.DefinitionId);
    if(!Def||!Def->SkillId.Equals(T.DefinitionId,ESearchCase::CaseSensitive)){M.State=EAetherInspectionState::Missing;M.Message=TEXT("技能定义不存在。");return;}
    if(T.SkillRank<0||T.SkillRank>Def->Ranks.Num()){Invalid(M,TEXT("技能节点等级无效。"));return;}
    M.Title=Def->DisplayName;M.IconId=Def->IconId;M.Category=TEXT("Skill");
    M.PermanentSkillRank=S.Skills.PermanentRank(Def->SkillId);
    M.EffectiveSkillRank=S.Skills.EffectiveRank(Def->SkillId,S.ExternalGrants);
    if(const auto* E=D.Effect(Def->SkillId,M.EffectiveSkillRank))M.CurrentSkillEffect=*E;
    if(const auto* E=D.Effect(Def->SkillId,M.PermanentSkillRank+1))M.NextSkillEffect=*E;
    if(const auto* E=D.Effect(Def->SkillId,T.SkillRank))M.SelectedSkillEffect=*E;
    Field(M,TEXT("permanentRank"),TEXT("永久等级"),FString::FromInt(M.PermanentSkillRank));
    Field(M,TEXT("effectiveRank"),TEXT("实际授权等级"),FString::FromInt(M.EffectiveSkillRank));
    Field(M,TEXT("points"),TEXT("可用技能点"),FString::FromInt(S.Skills.AvailableSkillPoints));
    Field(M,TEXT("cast"),TEXT("施法限制"),TEXT("需要满足施法距离、目标、材料、资源及当前动作条件。"));
    if(const auto* Story=S.Skills.StoryGrants.Find(Def->SkillId))Field(M,TEXT("story"),TEXT("故事授予"),*Story);
    for(const auto& Grant:S.ExternalGrants)if(Grant.SkillId==Def->SkillId)
        Field(M,TEXT("external"),Grant.Source==EAetherSkillGrantSource::Equipment?TEXT("装备授予"):TEXT("临时授予"),Grant.SourceId);
    for(const auto& P:Def->Prerequisites)
    {
        const auto* Before=D.Skills.Find(P.SkillId);
        Field(M,TEXT("prerequisite"),TEXT("前置技能"),FString::Printf(TEXT("%s %d / %d"),Before?*Before->DisplayName:*P.SkillId,S.Skills.PermanentRank(P.SkillId),P.Rank));
        Action(M,EAetherInspectAction::FocusSkill,P.SkillId,TEXT("查看前置"),true);
    }
    if(!Def->RequiredQuest.IsEmpty())
    {
        Field(M,TEXT("quest"),TEXT("前置任务"),Def->RequiredQuest);
        Action(M,EAetherInspectAction::TrackQuest,Def->RequiredQuest,TEXT("追踪任务"),true);
    }
    const auto Allowed=S.Skills.CanLearnNext(Def->SkillId,S.SkillContext,D);
    const bool NextNode=T.SkillRank==0||T.SkillRank==M.PermanentSkillRank+1;
    M.Message=NextNode?AetherInspection::SkillReason(Allowed):TEXT("仅能依次学习下一级；当前选择保持只读。");
    Action(M,EAetherInspectAction::Learn,Def->SkillId,TEXT("学习 / 升级"),NextNode&&Allowed==EAetherSkillMutationCode::Applied,M.Message,1,true);
    const bool CanBind=Def->bActive&&M.EffectiveSkillRank>0&&(T.SkillRank==0||T.SkillRank<=M.EffectiveSkillRank);
    for(int32 Slot=0;Slot<FAetherSkillStateV10::HotbarCapacity;++Slot)
        Action(M,EAetherInspectAction::BindHotbar,FString::Printf(TEXT("Hotbar.%d"),Slot+1),TEXT("绑定快捷位"),CanBind,CanBind?FString():TEXT("当前节点尚未获得可用授权。"));
}
}
FString AetherInspection::InventoryReason(EAetherInventoryMutationCode C)
{
    using E=EAetherInventoryMutationCode;
    switch(C)
    {
    case E::Applied:return {};
    case E::Missing:return TEXT("对象已不存在。");
    case E::Capacity:return TEXT("容量不足。");
    case E::Incompatible:return TEXT("物品状态不兼容。");
    case E::Occupied:return TEXT("槽位被其他装备额外占用，请先处理双手装备冲突。");
    case E::Locked:return TEXT("物品锁定或受任务保护。");
    case E::Bound:return TEXT("物品受绑定限制。");
    case E::Equipped:return TEXT("请先卸下装备。");
    case E::NotAllowed:return TEXT("该物品不允许此操作。");
    default:return TEXT("当前快照无效。");
    }
}
FString AetherInspection::SkillReason(EAetherSkillMutationCode C)
{
    using E=EAetherSkillMutationCode;
    switch(C)
    {
    case E::Applied:return {};
    case E::Unchanged:return TEXT("已经处于目标状态。");
    case E::NotReady:return TEXT("战斗、施法或冷却中暂不可学习。");
    case E::StoryRequired:return TEXT("请先完成导师的基础能力授予。");
    case E::Prerequisite:return TEXT("缺少永久前置技能。");
    case E::LevelRequired:return TEXT("角色等级不足。");
    case E::QuestRequired:return TEXT("前置任务尚未完成。");
    case E::InsufficientPoints:return TEXT("技能点不足。");
    case E::MaxRank:return TEXT("已达到最高永久等级。");
    case E::Missing:return TEXT("技能不存在。");
    default:return TEXT("当前技能状态不允许此操作。");
    }
}
FAetherInspectRequest AetherInspection::Pin(const FAetherInspectionSnapshot& S,FAetherInspectTarget T)
{
    if(T.Kind==EAetherInspectTarget::EquipmentSlot)T.InstanceId=S.Inventory.Equipment.FindRef(T.SlotId);
    if(T.Kind==EAetherInspectTarget::EquipmentSlot||T.Kind==EAetherInspectTarget::ItemInstance)
        if(const auto* I=S.Inventory.Find(T.InstanceId))T.DefinitionId=I->DefinitionId;
    return {S.Context,MoveTemp(T)};
}
FAetherInspectionModel AetherInspection::Build(const FAetherInspectRequest& R,const FAetherInspectionSnapshot& S,
    const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills)
{
    FAetherInspectionModel M;M.Request=R;
    if(!R.Context.IsValid()||!S.Context.IsValid()){Invalid(M,TEXT("尚无已授权的拥有者快照。"));return M;}
    if(!R.Context.Same(S.Context))
    {M.State=EAetherInspectionState::Changed;M.Message=TEXT("对象已变化，请重新查看后操作。");return M;}
    M.State=EAetherInspectionState::Ready;
    switch(R.Target.Kind)
    {
    case EAetherInspectTarget::ItemInstance:
    case EAetherInspectTarget::EquipmentSlot:Item(M,S,Items);break;
    case EAetherInspectTarget::ItemDefinition:
    {
        FString Reason;if(!Items.Validate(Reason)){Invalid(M,TEXT("物品定义无效。"));break;}
        if(const auto* D=Items.Items.Find(R.Target.DefinitionId);D&&D->Id.Equals(R.Target.DefinitionId,ESearchCase::CaseSensitive))
        {
            DefinitionFields(M,*D);
            if(S.Shop.IsSet()&&S.Shop->Products.Contains(D->Id)&&!S.TradeTargetStableId.IsEmpty()&&D->BuyPrice>0)
            {
                Field(M,TEXT("buyPrice"),TEXT("单件价格"),FString::FromInt(D->BuyPrice));
                Action(M,EAetherInspectAction::Buy,S.TradeTargetStableId,TEXT("购买"),S.bCanAct&&S.Gold>=D->BuyPrice,
                    S.Gold<D->BuyPrice?TEXT("金币不足"):TEXT("当前无法操作"),FMath::Max(1,FMath::Min(1000,S.Gold/D->BuyPrice)),true);
                M.Actions.Last().UnitPrice=D->BuyPrice;
            }
        }
        else {M.State=EAetherInspectionState::Missing;M.Message=TEXT("物品定义不存在。");}
        break;
    }
    case EAetherInspectTarget::SkillNode:Skill(M,S,Skills);break;
    case EAetherInspectTarget::StatusEffect:
    {
        if(!FMath::IsFinite(S.ServerTimeSeconds)||S.ServerTimeSeconds<0||S.StatusEffects.Num()>256)
        {Invalid(M,TEXT("状态效果快照无效。"));break;}
        TSet<FGuid> EffectIds;
        for(const auto& Effect:S.StatusEffects)
        {
            if(!Effect.InstanceId.IsValid()||EffectIds.Contains(Effect.InstanceId)||Effect.Impacts.Num()>32)
            {Invalid(M,TEXT("状态效果信息无效。"));return M;}
            EffectIds.Add(Effect.InstanceId);
        }
        const auto* E=S.StatusEffects.FindByPredicate([&](const auto& V){return V.InstanceId==R.Target.InstanceId&&V.InstanceId.IsValid();});
        if(!E||(!R.Target.DefinitionId.IsEmpty()&&!E->DefinitionId.Equals(R.Target.DefinitionId,ESearchCase::CaseSensitive))){M.State=EAetherInspectionState::Changed;M.Message=TEXT("状态效果已移除。");break;}
        if(E->ExpiresAtServerSeconds.IsSet()&&!FMath::IsFinite(E->ExpiresAtServerSeconds.GetValue()))
        {Invalid(M,TEXT("状态效果时间无效。"));break;}
        if(E->ExpiresAtServerSeconds.IsSet()&&E->ExpiresAtServerSeconds.GetValue()<=S.ServerTimeSeconds)
        {M.State=EAetherInspectionState::Changed;M.Message=TEXT("状态效果已结束。");break;}
        M.Title=E->DisplayName;M.IconId=E->IconId;M.Category=TEXT("StatusEffect");M.Fields=E->Impacts;
        Field(M,TEXT("source"),TEXT("效果来源"),E->Source);
        if(E->ExpiresAtServerSeconds.IsSet())M.RemainingSeconds=E->ExpiresAtServerSeconds.GetValue()-S.ServerTimeSeconds;
        else Field(M,TEXT("duration"),TEXT("持续时间"),TEXT("无固定时限"));
        break;
    }
    default:Invalid(M,TEXT("详情对象类型无效。"));break;
    }
    if(S.ProfileRevision<0||S.ProfileRevision==MAX_int64)
        for(auto& A:M.Actions)if(A.Kind!=EAetherInspectAction::FocusSkill&&A.Kind!=EAetherInspectAction::TrackQuest)
        {A.bEnabled=false;A.DisabledReason=TEXT("角色信息尚未就绪。");}
    return M;
}
