#pragma once
#include "CoreMinimal.h"

// 定义与实例分离。v9 反射 profile 保持冻结；v10 使用这些显式值对象构建持久 DTO。
struct FAetherV10ItemDefinition
{
    FString Id, DisplayName, Category, IconId, EquipmentId, UseId;
    int32 MaxStack=1, BuyPrice=0, SellPrice=0, MaxDurability=0;
    bool bSellable=false, bDroppable=true, bQuestLocked=false;
    TArray<FString> AllowedSlots, AdditionalOccupiedSlots;
    // 装备附加属性由适配器白名单消费；武器动作基础值仍由 EquipmentId 指向的配置定义。
    // 这些值不得直接写反应模拟器，也不能与基础值各自覆盖同一条权威配置。
    TMap<FString,double> Stats;
    double BrokenStatMultiplier=0.25;
};
struct FAetherEquipmentSlotDefinition
{
    FString Id;
    TArray<FString> AllowedCategories;
};
struct AETHERCORE_API FAetherV10ItemDefinitions
{
    int32 ContentSchemaVersion=3, DefaultCapacity=32;
    TMap<FString,FAetherV10ItemDefinition> Items;
    TArray<FAetherEquipmentSlotDefinition> Slots;
    bool Validate(FString& Reason) const;
    static FAetherV10ItemDefinitions Parse(const FString& Json, FString& Reason);
    const FAetherEquipmentSlotDefinition* FindSlot(const FString& Id) const;
};

struct AETHERCORE_API FAetherV10ItemInstance
{
    FGuid InstanceId;
    FString DefinitionId;
    int32 Quantity=1, SlotIndex=-1, Quality=0;
    int32 Durability=-1; // -1 表示定义禁用耐久，不能显示虚假耐久条。
    TMap<FString,int32> Affixes;
    FString BoundToCharacter, StateGroup;
    FGuid QuestInstanceId;
    bool bLocked=false, bFavorite=false;

    // 格子、GUID、容器归属不属于堆叠身份；所有可能被合并洗掉的实例状态都属于它。
    bool SameStackKey(const FAetherV10ItemInstance& Other) const;
};
struct FAetherInventoryTransition
{
    FGuid From, To;
    int32 Quantity=0;
};
enum class EAetherInventoryMutationCode : uint8
{
    Applied, Invalid, Missing, Capacity, Incompatible, Occupied, Locked, Bound, Equipped, NotAllowed
};
struct FAetherInventoryMutation
{
    EAetherInventoryMutationCode Code=EAetherInventoryMutationCode::Invalid;
    int32 ActualQuantity=0;
    TArray<FGuid> AffectedIds;
    TArray<FAetherInventoryTransition> Transitions;
};

// Slots 是真实持久位置，不是过滤视图索引；空格通过 Capacity 与未占用索引明确表示。
// Equipment 只引用仍在库存内的实例，装备和卸下不会改变库存容量。
struct AETHERCORE_API FAetherInventoryStateV10
{
    int32 Capacity=32;
    TArray<FAetherV10ItemInstance> Items;
    TMap<FString,FGuid> Equipment;
    const FAetherV10ItemInstance* Find(FGuid Id) const;
    const FAetherV10ItemInstance* At(int32 Slot) const;
    int32 FirstEmpty() const;
    bool IsEquipped(FGuid Id) const;
    bool Validate(const FAetherV10ItemDefinitions& Definitions, FString& Reason) const;

    // 以下均先计算副本并校验，再一次性替换本值对象；数据库发布仍由事务协调者负责。
    // 新购/奖励只生成定义默认状态；先填相同完整 StackKey 的堆，再占空格，容量不足全回滚。
    FAetherInventoryMutation AddNew(const FString& DefinitionId,int32 Quantity,const FAetherV10ItemDefinitions& Definitions);
    FAetherInventoryMutation RemoveForSale(FGuid Id,int32 Quantity,const FString& ServerCharacterId,const FAetherV10ItemDefinitions& Definitions);
    FAetherInventoryMutation Move(FGuid Id,int32 Destination,const FAetherV10ItemDefinitions& Definitions);
    FAetherInventoryMutation Swap(FGuid A,FGuid B,const FAetherV10ItemDefinitions& Definitions);
    FAetherInventoryMutation Split(FGuid Id,int32 Quantity,FGuid NewId,int32 Destination,const FAetherV10ItemDefinitions& Definitions);
    FAetherInventoryMutation Merge(FGuid From,FGuid To,int32 Quantity,const FAetherV10ItemDefinitions& Definitions);
    FAetherInventoryMutation SetLocked(FGuid Id,bool Enabled,const FAetherV10ItemDefinitions& Definitions);
    FAetherInventoryMutation SetFavorite(FGuid Id,bool Enabled,const FAetherV10ItemDefinitions& Definitions);
    FAetherInventoryMutation Sort(bool MergeStacks,const FAetherV10ItemDefinitions& Definitions);
    FAetherInventoryMutation Equip(FGuid Id,const FString& Slot,const FString& ServerCharacterId,const FAetherV10ItemDefinitions& Definitions);
    FAetherInventoryMutation Unequip(FGuid Id,const FAetherV10ItemDefinitions& Definitions);
    FAetherInventoryMutation Wear(FGuid Id,int32 Amount,const FAetherV10ItemDefinitions& Definitions);
    // 修复只修改候选耐久；扣款和服务权限必须在同一个 profile 事务内进行。
    FAetherInventoryMutation Repair(FGuid Id,const FAetherV10ItemDefinitions& Definitions);
    EAetherInventoryMutationCode CanRemove(FGuid Id,const FString& ServerCharacterId,bool ForSale,const FAetherV10ItemDefinitions& Definitions) const;
    TMap<FString,double> EquippedStats(const FAetherV10ItemDefinitions& Definitions) const;
private:
    FAetherInventoryMutation Publish(FAetherInventoryStateV10&& Candidate,FAetherInventoryMutation Result,const FAetherV10ItemDefinitions& Definitions);
};
