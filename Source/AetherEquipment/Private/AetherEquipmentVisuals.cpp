#include "AetherEquipmentVisuals.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"

TArray<FAetherEquipmentVisualSpec> AetherEquipmentVisuals::Resolve(
    const UAetherEquipmentCatalog* Catalog,const TArray<FAetherEquippedSlot>& Slots)
{
    TArray<FAetherEquipmentVisualSpec> Result;
    if(!Catalog)return Result;
    TSet<FName> Seen;
    for(const auto& Slot:Slots)
    {
        const auto* D=Catalog->Find(Slot.ItemId);
        if(!D||D->bInvisibleAccessory||Seen.Contains(Slot.Slot)||Slot.Slot.IsNone()||D->Socket.IsNone()||D->GripTransform.ContainsNaN())continue;
        Seen.Add(Slot.Slot);Result.Add({Slot.Slot,D->ItemId,D->Socket,D->Mesh,D->GripTransform});
        if(!D->SecondarySocket.IsNone())
            Result.Add({FName(*(Slot.Slot.ToString()+TEXT(".Pair"))),D->ItemId,D->SecondarySocket,D->Mesh,D->SecondaryGripTransform});
    }
    Result.Sort([](const auto& A,const auto& B){return A.Slot.LexicalLess(B.Slot);});
    return Result;
}
bool AetherEquipmentVisuals::Attach(USkinnedMeshComponent* Body,UStaticMeshComponent* Visual,
    const FAetherEquipmentVisualSpec& Spec,UStaticMesh* Mesh)
{
    if(!Body||!Visual||!Mesh||!Body->DoesSocketExist(Spec.Socket))return false;
    Visual->SetStaticMesh(Mesh);Visual->SetMobility(EComponentMobility::Movable);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);Visual->SetGenerateOverlapEvents(false);
    Visual->SetCanEverAffectNavigation(false);
    if(Visual->IsRegistered())Visual->AttachToComponent(Body,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Spec.Socket);
    else Visual->SetupAttachment(Body,Spec.Socket);
    // 网格按厘米制作。只继承 Socket 位姿，不继承 FBX 骨架中可能存在的 100 倍单位缩放。
    Visual->SetAbsolute(false,false,true);Visual->SetRelativeTransform(Spec.GripTransform);
    return true;
}
