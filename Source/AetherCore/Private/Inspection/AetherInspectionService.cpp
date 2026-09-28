#include "Inspection/AetherInspectionService.h"
#include "Inventory/AetherItemEligibility.h"

namespace
{
FString ItemFingerprint(const FAetherV10ItemInstance& I)
{
    FString Key=I.InstanceId.ToString()+TEXT("|")+I.DefinitionId+TEXT("|")+LexToString(I.Quantity)+TEXT("|")+
        LexToString(I.SlotIndex)+TEXT("|")+LexToString(I.Quality)+TEXT("|")+LexToString(I.Durability)+TEXT("|")+I.BoundToCharacter+TEXT("|")+I.StateGroup+
        TEXT("|")+I.QuestInstanceId.ToString()+TEXT("|")+(I.bLocked?TEXT("1"):TEXT("0"))+(I.bFavorite?TEXT("1"):TEXT("0"));
    TArray<FString> Names;I.Affixes.GenerateKeyArray(Names);Names.Sort();
    for(const FString& Name:Names)Key+=TEXT("|")+Name+TEXT("=")+LexToString(I.Affixes.FindChecked(Name));
    return Key;
}
void IndexInventory(const FAetherInventoryStateV10& Inventory,FAetherInspectionSnapshot::FInventoryLookup& Lookup)
{
    Lookup={};
    for(int32 Index=0;Index<Inventory.Items.Num();++Index)
    {
        const auto& Item=Inventory.Items[Index];
        Lookup.BySlot.Add(Item.SlotIndex,Index);Lookup.ById.Add(Item.InstanceId,Index);
        Lookup.Fingerprints.Add(Item.InstanceId,ItemFingerprint(Item));
    }
    TArray<FString> Slots;Inventory.Equipment.GenerateKeyArray(Slots);Slots.Sort();
    for(const FString& Slot:Slots)
    {
        const FGuid Id=Inventory.Equipment.FindChecked(Slot);
        Lookup.LoadoutFingerprint+=Slot+TEXT("=")+Id.ToString()+TEXT(":")+Lookup.Fingerprints.FindRef(Id)+TEXT(";");
    }
}
}
void FAetherInspectionSnapshot::RebuildLookup()
{
    IndexInventory(Inventory,InventoryLookup);
    ContainerLookup={};if(Container.IsSet())IndexInventory(Container->Inventory,ContainerLookup);
}
const FAetherV10ItemInstance* FAetherInspectionSnapshot::At(int32 Slot,bool bContainer) const
{
    const auto& State=bContainer&&Container.IsSet()?Container->Inventory:Inventory;
    const auto& Lookup=bContainer?ContainerLookup:InventoryLookup;
    if(const int32* Index=Lookup.BySlot.Find(Slot))return State.Items.IsValidIndex(*Index)?&State.Items[*Index]:nullptr;
    return Lookup.BySlot.IsEmpty()&&!State.Items.IsEmpty()?State.At(Slot):nullptr;
}
const FAetherV10ItemInstance* FAetherInspectionSnapshot::Find(FGuid Id,bool bContainer) const
{
    const auto& State=bContainer&&Container.IsSet()?Container->Inventory:Inventory;
    const auto& Lookup=bContainer?ContainerLookup:InventoryLookup;
    if(const int32* Index=Lookup.ById.Find(Id))return State.Items.IsValidIndex(*Index)?&State.Items[*Index]:nullptr;
    return Lookup.ById.IsEmpty()&&!State.Items.IsEmpty()?State.Find(Id):nullptr;
}

namespace
{
void Field(FAetherInspectionModel& M,FString Key,FString Label,FString Value)
{M.Fields.Add({MoveTemp(Key),MoveTemp(Label),MoveTemp(Value)});}
void Action(FAetherInspectionModel& M,EAetherInspectAction Kind,FString Argument,const TCHAR* Label,
    bool Enabled,FString Reason={},int32 Max=1,bool Confirm=false)
{M.Actions.Add({Kind,MoveTemp(Argument),Label,MoveTemp(Reason),Max,Enabled,Confirm});}
void Invalid(FAetherInspectionModel& M,const TCHAR* Message)
{M.State=EAetherInspectionState::Invalid;M.Message=Message;M.Actions.Reset();}
FString DisplayId(const FString& Id)
{
    static const TMap<FString,FString> Names={
        {TEXT("Consumable"),TEXT("消耗品")},{TEXT("Weapon"),TEXT("武器")},{TEXT("Shield"),TEXT("盾牌")},
        {TEXT("Armor"),TEXT("防具")},{TEXT("Accessory"),TEXT("饰品")},{TEXT("Material"),TEXT("材料")},
        {TEXT("MainHand"),TEXT("主手")},{TEXT("OffHand"),TEXT("副手")},{TEXT("Head"),TEXT("头部")},
        {TEXT("Chest"),TEXT("胸部")},{TEXT("Hands"),TEXT("手部")},{TEXT("Legs"),TEXT("腿部")},
        {TEXT("Feet"),TEXT("足部")},{TEXT("Neck"),TEXT("项链")},{TEXT("Ring1"),TEXT("戒指一")},
        {TEXT("Ring2"),TEXT("戒指二")},{TEXT("MaxHealth"),TEXT("生命上限")},
        {TEXT("MaxMana"),TEXT("法力上限")},{TEXT("MaxStamina"),TEXT("耐力上限")},
        {TEXT("FrostResist"),TEXT("冰霜抗性")},{TEXT("WaterResist"),TEXT("水系抗性")},
        {TEXT("Light"),TEXT("轻击")},{TEXT("Heavy"),TEXT("重击")}};
    if(const FString* Name=Names.Find(Id))return *Name;
    return Id;
}
void DynamicUseFields(FAetherInspectionModel& M,const FAetherV10ItemDefinition& D,const FAetherInspectionSnapshot& S)
{
    if(D.UseId.IsEmpty())return;
    const auto* Rule=S.UseRules.Find(D.UseId);
    if(!Rule)return;
    if(S.UseSummary.bKnown)
    {
        Field(M,TEXT("benefit"),TEXT("当前预计实际恢复"),FString::Printf(TEXT("生命 %.0f / 法力 %.0f / 耐力 %.0f"),
            FMath::Clamp(Rule->Health,0.,FMath::Max(0.,S.UseSummary.MaxHealth-S.UseSummary.Health)),
            FMath::Clamp(Rule->Mana,0.,FMath::Max(0.,S.UseSummary.MaxMana-S.UseSummary.Mana)),
            FMath::Clamp(Rule->Stamina,0.,FMath::Max(0.,S.UseSummary.MaxStamina-S.UseSummary.Stamina))));
        Field(M,TEXT("cooldownRemaining"),TEXT("当前共享冷却剩余"),FString::Printf(TEXT("%.1f 秒"),S.UseSummary.CooldownRemaining));
    }
    else Field(M,TEXT("benefit"),TEXT("当前预计实际恢复"),TEXT("等待角色资源信息"));
}
void DefinitionFields(FAetherInspectionModel& M,const FAetherV10ItemDefinition& D,const FAetherInspectionSnapshot& S)
{
    M.Title=D.DisplayName;M.IconId=D.IconId;M.Category=D.Category;
    Field(M,TEXT("category"),TEXT("类别"),DisplayId(D.Category));
    if(!D.UseId.IsEmpty())
    {
        if(const auto* Rule=S.UseRules.Find(D.UseId))
        {
            Field(M,TEXT("use"),TEXT("基础恢复"),FString::Printf(TEXT("生命 %.0f / 法力 %.0f / 耐力 %.0f"),Rule->Health,Rule->Mana,Rule->Stamina));
            Field(M,TEXT("cooldown"),TEXT("基础共享冷却"),FString::Printf(TEXT("%.1f 秒"),Rule->Cooldown));
            Field(M,TEXT("restriction"),TEXT("基础使用条件"),FString::Printf(TEXT("受击后至少等待 %.1f 秒；仍需满足资源和动作条件"),Rule->SafeSeconds));
        }
        else Field(M,TEXT("use"),TEXT("基础用途"),TEXT("使用规则尚未加载"));
        DynamicUseFields(M,D,S);
    }
    if(!D.AllowedSlots.IsEmpty())
    {
        TArray<FString> Slots;for(const FString& Slot:D.AllowedSlots)Slots.Add(DisplayId(Slot));
        Field(M,TEXT("slots"),TEXT("可装备槽"),FString::Join(Slots,TEXT(" / ")));
    }
    if(!D.AdditionalOccupiedSlots.IsEmpty())
    {
        TArray<FString> Slots;for(const FString& Slot:D.AdditionalOccupiedSlots)Slots.Add(DisplayId(Slot));
        Field(M,TEXT("occupied"),TEXT("额外占用"),FString::Join(Slots,TEXT(" / ")));
    }
    if(!D.Stats.IsEmpty())
    {
        TArray<FString> Keys;D.Stats.GenerateKeyArray(Keys);Keys.Sort();
        for(const FString& Key:Keys)
        {
            const FString Label=Key==TEXT("Armor")?TEXT("护甲"):DisplayId(Key);
            Field(M,TEXT("stat:")+Key,FString(TEXT("本件基础附加 · "))+Label,FString::Printf(TEXT("%+.1f"),D.Stats.FindChecked(Key)));
        }
    }
    if(!D.EquipmentId.IsEmpty())
    {
        if(const auto* Equipment=S.EquipmentPreviews.Find(D.EquipmentId))
        {
            if(Equipment->bTwoHanded)Field(M,TEXT("twoHanded"),TEXT("持握"),TEXT("双手；占用副手"));
            if(Equipment->bAllowsGuard)
                Field(M,TEXT("guard"),TEXT("格挡基础"),FString::Printf(TEXT("耐力消耗系数 %.2f；招架窗口 %.2f 秒"),Equipment->GuardStaminaMultiplier,Equipment->ParryWindowSeconds));
            for(const auto& Attack:Equipment->Attacks)
                Field(M,TEXT("attack:")+Attack.Id,TEXT("基础动作 · ")+DisplayId(Attack.Id),
                    FString::Printf(TEXT("基础伤害 %.1f · 姿态伤害 %.1f · 时长 %.2f 秒 · 距离 %.0f 厘米 · 半径 %.0f 厘米"),
                        Attack.Damage,Attack.PostureDamage,Attack.DurationSeconds,Attack.ReachCm,Attack.RadiusCm));
        }
        else Field(M,TEXT("equipmentPending"),TEXT("装备动作"),TEXT("装备定义尚未加载，基础动作数据暂不可用"));
    }
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
    for(const auto& Key:Ordered)M.Comparison.Add({Key==TEXT("Armor")?TEXT("护甲"):DisplayId(Key),Before.FindRef(Key),After.FindRef(Key)});
    // 双手替换的副手也必须列出，而非只比较目标格子的单件数据。
    for(const auto& P:S.Inventory.Equipment)
        if(!Candidate.IsEquipped(P.Value))M.DisplacedInstances.AddUnique(P.Value);
    M.DisplacedInstances.Sort([](const FGuid& A,const FGuid& B){return A.ToString()<B.ToString();});
    M.ComparisonMessage=TEXT("换装后属性变化：仅比较装备附加值，包含耐久折减及卸装；不计算最终伤害或战斗强弱。");
    const auto Grants=[&](const FAetherInventoryStateV10& Inventory)
    {
        TMap<FString,int32> R;TSet<FGuid> Seen;
        for(const auto& Slot:Inventory.Equipment)
        {
            if(Seen.Contains(Slot.Value))continue;Seen.Add(Slot.Value);
            const auto* I=Inventory.Find(Slot.Value);if(!I)continue;const auto& Def=D.Items.FindChecked(I->DefinitionId);
            if(Def.MaxDurability>0&&I->Durability==0)continue;
            for(const auto& G:Def.SkillGrants)R.FindOrAdd(G.Key)=FMath::Max(R.FindRef(G.Key),G.Value);
        }
        return R;
    };
    const auto OldGrants=Grants(S.Inventory),NewGrants=Grants(Candidate);TSet<FString> GrantIds;
    for(const auto& G:OldGrants)GrantIds.Add(G.Key);for(const auto& G:NewGrants)GrantIds.Add(G.Key);
    TArray<FString> OrderedGrants=GrantIds.Array();OrderedGrants.Sort();
    for(const auto& Id:OrderedGrants)if(OldGrants.FindRef(Id)!=NewGrants.FindRef(Id))
        Field(M,TEXT("gearSkill"),TEXT("装备来源技能等级变化"),FString::Printf(TEXT("%s：%d → %d（永久学习保留）"),*Id,OldGrants.FindRef(Id),NewGrants.FindRef(Id)));
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
    const auto& Def=D.Items.FindChecked(I->DefinitionId);DefinitionFields(M,Def,S);M.Item=*I;
    Field(M,TEXT("quantity"),TEXT("数量"),FString::FromInt(I->Quantity));
    Field(M,TEXT("quality"),TEXT("品质元数据（不提供战斗加成）"),FString::FromInt(I->Quality));
    if(!I->Affixes.IsEmpty())Field(M,TEXT("affixes"),TEXT("词条"),TEXT("保存的实例元数据；当前不参与战斗数值。"));
    Field(M,TEXT("locked"),TEXT("锁定"),I->bLocked?TEXT("已锁定"):TEXT("未锁定"));
    Field(M,TEXT("favorite"),TEXT("收藏"),I->bFavorite?TEXT("已收藏"):TEXT("未收藏"));
    Field(M,TEXT("binding"),TEXT("绑定"),I->BoundToCharacter.IsEmpty()?TEXT("未绑定"):I->BoundToCharacter);
    if(Def.MaxDurability>0)
        Field(M,TEXT("durability"),TEXT("耐久"),FString::Printf(TEXT("%d / %d"),I->Durability,Def.MaxDurability));
    else Field(M,TEXT("durability"),TEXT("耐久"),TEXT("不适用"));
    if(Def.MaxDurability>0)
    {
        const TCHAR* Behavior=Def.BrokenBehavior==EAetherBrokenBehavior::DisableAttack?TEXT("损坏后不能攻击"):
            Def.BrokenBehavior==EAetherBrokenBehavior::DisableGuard?TEXT("损坏后不能格挡"):
            Def.BrokenBehavior==EAetherBrokenBehavior::ScaleBaseAttack?TEXT("损坏后按配置折减基础攻击"):TEXT("损坏仅折减附加属性，保留基础攻击与格挡");
        Field(M,TEXT("brokenPolicy"),TEXT("损坏规则"),FString(Behavior)+TEXT("；装备授予技能在损坏时撤销，维修后恢复。"));
    }
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
    if(!Def.UseId.IsEmpty())
    {
        const auto Static=AetherItemEligibility::Query(S.Inventory,I->InstanceId,S.Context.OwnerIdentity,EAetherItemOperation::Use,D);
        const auto* Rule=S.UseRules.Find(Def.UseId);
        const auto Use=Rule?AetherItemEligibility::QueryUse(*Rule,S.UseSummary):EAetherUseAvailability::Unknown;
        Action(M,EAetherInspectAction::Use,{},TEXT("使用"),Static==EAetherInventoryMutationCode::Applied&&Use==EAetherUseAvailability::Allowed,
            Static==EAetherInventoryMutationCode::Applied?AetherItemEligibility::UseReason(Use):AetherInspection::InventoryReason(Static));
    }
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
    Field(M,TEXT("cast"),Def->bActive?TEXT("施法限制"):TEXT("被动效果"),Def->bActive?TEXT("需要满足施法距离、目标、材料、资源及当前动作条件。"):TEXT("授权期间自动生效，来源合并取最高等级；不能拖入施法栏。"));
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
    for(const FString& Root:TArray<FString>{Def->SkillId,FString()})
    {
        auto Candidate=S.Skills;const auto Result=Candidate.Reset(Root,S.SkillContext,D,S.ExternalGrants);
        if(Result.Code==EAetherSkillMutationCode::Applied)
        {
            TArray<FString> Names;for(const auto& Id:Result.AffectedSkills)
            {
                const auto* Affected=D.Skills.Find(Id);
                Names.Add(FString::Printf(TEXT("%s %d → %d"),Affected?*Affected->DisplayName:*Id,S.Skills.PermanentRank(Id),Candidate.PermanentRank(Id)));
            }
            Field(M,Root.IsEmpty()?TEXT("resetAll"):TEXT("resetBranch"),Root.IsEmpty()?TEXT("重置全部预览"):TEXT("当前分支重置预览"),FString::Join(Names,TEXT("；")));
            Field(M,TEXT("refund"),TEXT("按实际支付账本退还"),FString::Printf(TEXT("%d 点；故事基础与外部授权保留"),Result.PointsChanged));
        }
        Action(M,EAetherInspectAction::ResetSkills,Root,Root.IsEmpty()?TEXT("重置全部自由学习"):TEXT("重置当前分支"),
            Result.Code==EAetherSkillMutationCode::Applied,AetherInspection::SkillReason(Result.Code),1,true);
        if(Result.Code==EAetherSkillMutationCode::Applied)
        {
            M.Actions.Last().ConfirmationSummary=FString::Printf(TEXT("退还实际支付的 %d 技能点。"),Result.PointsChanged);
            for(const auto& Id:Result.AffectedSkills)M.Actions.Last().ConfirmationSummary+=LINE_TERMINATOR+Id+FString::Printf(TEXT("：%d → %d"),S.Skills.PermanentRank(Id),Candidate.PermanentRank(Id));
            M.Actions.Last().ConfirmationSummary+=FString(LINE_TERMINATOR)+TEXT("故事基础与装备/临时授权保留。");
        }
    }
    const bool CanBind=Def->bActive&&M.EffectiveSkillRank>0&&(T.SkillRank==0||T.SkillRank<=M.EffectiveSkillRank);
    for(int32 Slot=0;Slot<FAetherSkillStateV10::HotbarCapacity;++Slot)
        Action(M,EAetherInspectAction::BindHotbar,FString::Printf(TEXT("Hotbar.%d"),Slot+1),TEXT("绑定快捷位"),CanBind,CanBind?FString():TEXT("当前节点尚未获得可用授权。"));
    for(int32 Slot=0;Slot<FAetherSkillStateV10::HotbarCapacity;++Slot)
        if(S.Skills.Hotbar.FindRef(Slot)==Def->SkillId)
            Action(M,EAetherInspectAction::UnbindHotbar,FString::Printf(TEXT("Hotbar.%d"),Slot+1),
                FString::Printf(TEXT("解除快捷位 %d"),Slot+1),true,FString());
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
FString AetherInspection::DependencyKey(const FAetherInspectionSnapshot& S,const FAetherInspectTarget& T)
{
    FString Key;const auto Add=[&](const FString& V){Key+=FString::FromInt(V.Len())+TEXT(":")+V;};
    Add(S.Context.OwnerIdentity);Add(S.Context.SessionId.ToString());
    if(T.Kind==EAetherInspectTarget::ItemInstance||T.Kind==EAetherInspectTarget::EquipmentSlot)
    {
        const bool bContainer=!T.ContainerId.IsEmpty();
        const auto& Lookup=bContainer?S.ContainerLookup:S.InventoryLookup;
        Add(T.InstanceId.ToString());
        Add(bContainer?S.ContainerContext.ToString():FString());Add(T.ContainerId);
        if(T.Kind==EAetherInspectTarget::ItemInstance&&!T.SlotId.IsEmpty())
        {
            int32 Slot=INDEX_NONE;
            if(LexTryParseString(Slot,*T.SlotId))
            {Add(T.SlotId);const auto* Occupant=S.At(Slot,bContainer);Add(Occupant?Occupant->InstanceId.ToString():TEXT("empty"));}
        }
        if(T.Kind==EAetherInspectTarget::EquipmentSlot)
        {Add(T.SlotId);Add(S.Inventory.Equipment.FindRef(T.SlotId).ToString());}
        if(const FString* Fingerprint=Lookup.Fingerprints.Find(T.InstanceId))Add(*Fingerprint);
        else if(const auto* Item=S.Find(T.InstanceId,bContainer))Add(ItemFingerprint(*Item));
        else Add(TEXT("missing"));
        if(!T.ComparisonSlot.IsEmpty()){Add(T.ComparisonSlot);Add(S.InventoryLookup.LoadoutFingerprint);}
    }
    else if(T.Kind==EAetherInspectTarget::ItemDefinition)
    {Add(S.TradeTargetStableId);Add(S.Shop.IsSet()?S.Shop->Id:FString());}
    else if(T.Kind==EAetherInspectTarget::SkillNode)Add(LexToString(S.ProfileRevision));
    // Status objects are identified and checked for expiry by Build; render cadence is not identity.
    return Key;
}
FAetherInspectRequest AetherInspection::Pin(const FAetherInspectionSnapshot& S,FAetherInspectTarget T)
{
    if(T.Kind==EAetherInspectTarget::EquipmentSlot)T.InstanceId=S.Inventory.Equipment.FindRef(T.SlotId);
    if(T.Kind==EAetherInspectTarget::EquipmentSlot||T.Kind==EAetherInspectTarget::ItemInstance)
    {
        const bool Container=!T.ContainerId.IsEmpty()&&S.Container.IsSet()&&S.Container->ContainerId.Equals(T.ContainerId,ESearchCase::CaseSensitive);
        if(const auto* I=S.Find(T.InstanceId,Container))T.DefinitionId=I->DefinitionId;
    }
    const FString Key=DependencyKey(S,T);return {S.Context,MoveTemp(T),Key};
}
bool AetherInspection::RevalidateIntent(const FAetherInspectionSnapshot& S,const FAetherInspectRequest& R,FAetherInspectRequest& Current)
{
    if(!S.Context.IsValid()||!R.Context.IsValid()||R.Context.SessionId!=S.Context.SessionId||
        !R.Context.OwnerIdentity.Equals(S.Context.OwnerIdentity,ESearchCase::CaseSensitive)||R.DependencyKey.IsEmpty())return false;
    if(R.Target.Kind!=EAetherInspectTarget::ItemInstance&&R.Target.Kind!=EAetherInspectTarget::EquipmentSlot)return false;
    const bool Container=!R.Target.ContainerId.IsEmpty();
    if(Container&&(!S.Container.IsSet()||!S.Container->bActive||!S.ContainerContext.IsValid()||
        !S.Container->ContainerId.Equals(R.Target.ContainerId,ESearchCase::CaseSensitive)))return false;
    auto Pinned=Pin(S,R.Target);
    if(Pinned.Target.InstanceId!=R.Target.InstanceId||Pinned.DependencyKey!=R.DependencyKey)return false;
    if(R.Target.InstanceId.IsValid()&&!S.Find(R.Target.InstanceId,Container))return false;
    Current=MoveTemp(Pinned);return true;
}
FAetherInspectionModel AetherInspection::Build(const FAetherInspectRequest& R,const FAetherInspectionSnapshot& S,
    const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills)
{
    FAetherInspectionModel M;M.Request=R;
    if(!R.Context.IsValid()||!S.Context.IsValid()){Invalid(M,TEXT("尚无已授权的拥有者快照。"));return M;}
    if(R.Context.SessionId!=S.Context.SessionId||!R.Context.OwnerIdentity.Equals(S.Context.OwnerIdentity,ESearchCase::CaseSensitive)||
        (R.DependencyKey.IsEmpty()?!R.Context.Same(S.Context):R.DependencyKey!=DependencyKey(S,R.Target)))
    {M.State=EAetherInspectionState::Changed;M.Message=TEXT("对象已变化，请重新查看后操作。");return M;}
    M.State=EAetherInspectionState::Ready;
    M.Request.Context=S.Context;
    if(!R.Target.ContainerId.IsEmpty())
    {
        if(R.Target.Kind!=EAetherInspectTarget::ItemInstance||!S.Container.IsSet()||!S.ContainerContext.IsValid()||
            !S.Container->ContainerId.Equals(R.Target.ContainerId,ESearchCase::CaseSensitive)||!S.Container->bActive)
        {M.State=EAetherInspectionState::Changed;M.Message=TEXT("容器会话已失效。");return M;}
        // 复用物品字段，操作能力再收窄为取出，不能直接穿戴或消耗容器中的物品。
        auto View=S;View.Inventory=S.Container->Inventory;View.Shop.Reset();View.Container.Reset();View.RebuildLookup();
        auto Local=R;Local.Target.ContainerId.Reset();Local.Context=S.Context;Local.DependencyKey=DependencyKey(View,Local.Target);
        auto ContainerItems=Items;ContainerItems.DefaultCapacity=View.Inventory.Capacity;
        M=Build(Local,View,ContainerItems,Skills);M.Request=R;M.Request.Context=S.Context;
        M.Actions.Reset();M.ComparisonSlots.Reset();M.Comparison.Reset();
        if(M.CanInteract()&&M.Item.IsSet())
            Action(M,EAetherInspectAction::Withdraw,R.Target.ContainerId,S.Container->Kind==EAetherContainerKind::WorldDrop?TEXT("拾取"):TEXT("取出"),
                S.bCanAct&&S.ContainerWorldRevision>=0,TEXT("需要当前容器授权与可操作状态"),M.Item->Quantity,true);
        return M;
    }
    switch(R.Target.Kind)
    {
    case EAetherInspectTarget::ItemInstance:
    case EAetherInspectTarget::EquipmentSlot:Item(M,S,Items);break;
    case EAetherInspectTarget::ItemDefinition:
    {
        FString Reason;if(!Items.Validate(Reason)){Invalid(M,TEXT("物品定义无效。"));break;}
        if(const auto* D=Items.Items.Find(R.Target.DefinitionId);D&&D->Id.Equals(R.Target.DefinitionId,ESearchCase::CaseSensitive))
        {
            DefinitionFields(M,*D,S);
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
    if(M.CanInteract()&&M.Item.IsSet()&&S.Container.IsSet()&&S.ContainerContext.IsValid()&&S.Container->Kind!=EAetherContainerKind::WorldDrop)
    {
        const auto& I=M.Item.GetValue();
        const auto Eligibility=AetherItemEligibility::Query(S.Inventory,I.InstanceId,S.Context.OwnerIdentity,
            S.Container->Kind==EAetherContainerKind::PersonalStorage?EAetherItemOperation::PersonalStorage:EAetherItemOperation::SharedStorage,Items);
        const bool Allowed=Eligibility==EAetherInventoryMutationCode::Applied;
        Action(M,EAetherInspectAction::Deposit,S.Container->ContainerId,TEXT("存入容器"),Allowed&&S.bCanAct&&S.ContainerWorldRevision>=0,
            Allowed?TEXT("需要当前容器授权与可操作状态"):AetherInspection::InventoryReason(Eligibility),I.Quantity,true);
        if(S.Container->Kind==EAetherContainerKind::PersonalStorage&&(I.QuestInstanceId.IsValid()||Items.Items.FindChecked(I.DefinitionId).bQuestLocked))
            Field(M,TEXT("storageQuest"),TEXT("任务物品保管"),TEXT("存入后保留任务身份；交付任务可能需要先取回背包。"));
    }
    if(S.ProfileRevision<0||S.ProfileRevision==MAX_int64)
        for(auto& A:M.Actions)if(A.Kind!=EAetherInspectAction::FocusSkill&&A.Kind!=EAetherInspectAction::TrackQuest)
        {A.bEnabled=false;A.DisabledReason=TEXT("角色信息尚未就绪。");}
    return M;
}
