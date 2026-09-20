#pragma once
#include "CoreMinimal.h"
#include "AetherEquipmentComponent.h"

class USkinnedMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;

// 真实身体和独立预览消费同一份纯展示配置，不复制任何攻击、ASC 或库存组件。
struct FAetherEquipmentVisualSpec
{
    FName Slot,ItemId,Socket;
    TSoftObjectPtr<UStaticMesh> Mesh;
    FTransform GripTransform;
    bool SameAppearance(const FAetherEquipmentVisualSpec& Other) const
    {return Slot==Other.Slot&&ItemId==Other.ItemId&&Socket==Other.Socket&&Mesh==Other.Mesh&&GripTransform.Equals(Other.GripTransform);}
};
namespace AetherEquipmentVisuals
{
    AETHEREQUIPMENT_API TArray<FAetherEquipmentVisualSpec> Resolve(
        const UAetherEquipmentCatalog* Catalog,const TArray<FAetherEquippedSlot>& Slots);
    // 返回 false 表示骨架没有对应 Socket，不能把物品悄悄挂到身体原点。
    AETHEREQUIPMENT_API bool Attach(USkinnedMeshComponent* Body,UStaticMeshComponent* Visual,
        const FAetherEquipmentVisualSpec& Spec,UStaticMesh* Mesh);
}
