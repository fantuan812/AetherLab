#include "AetherEquipmentVisuals.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"

namespace
{
bool SocketLocal(const USkinnedMeshComponent* Body, FName SocketName, FName MainBone, FTransform& Out)
{
    Out=FTransform::Identity;
    if(!Body||MainBone.IsNone()||Body->GetBoneIndex(MainBone)==INDEX_NONE)return false;
    if(const auto* Socket=Body->GetSocketByName(SocketName))
    {
        if(Socket->BoneName!=MainBone)return false;
        Out=Socket->GetSocketLocalTransform();
        return AetherEquipmentGrip::IsUniformTransform(Out);
    }
    // A bone-name attachment is an explicit identity socket, not a missing-socket fallback.
    return SocketName==MainBone;
}
}

bool AetherEquipmentVisuals::ResolveSupportGrip(const USkinnedMeshComponent* Body,
    const UAetherEquipmentDefinition* D,FName MainBone,FName SupportBone,FAetherResolvedWeaponGrip& Out,FString& Reason)
{
    Out=FAetherResolvedWeaponGrip();Reason.Reset();
    if(!Body||!D||!D->bOccupiesBothHands||!D->HasValidSupportHandGrip())
    {Reason=TEXT("Missing complete two-hand grip contract");return false;}
    if(D->GripTargetMesh.ToSoftObjectPath()!=FSoftObjectPath(Body->GetSkinnedAsset())||
        D->GripMainHandBone!=MainBone||D->GripSupportHandBone!=SupportBone||Body->GetBoneIndex(SupportBone)==INDEX_NONE)
    {Reason=TEXT("Grip body/hand bones do not match the active animation binding");return false;}
    FTransform Local;
    if(!SocketLocal(Body,D->Socket,MainBone,Local)||!AetherEquipmentGrip::IsUniformTransform(Body->GetComponentTransform()))
    {Reason=TEXT("Grip socket parent or uniform positive scale is invalid");return false;}
    Out.bValid=true;Out.MainBone=MainBone;Out.SupportBone=SupportBone;Out.SocketToMainBone=Local;
    Out.WeaponToSocket=D->GripTransform;Out.SupportHandToWeapon=D->SupportHandTransform;
    return true;
}

TArray<FAetherEquipmentVisualSpec> AetherEquipmentVisuals::Resolve(
    const UAetherEquipmentCatalog* Catalog,const TArray<FAetherEquippedSlot>& Slots)
{
    TArray<FAetherEquipmentVisualSpec> Result;
    if(!Catalog)return Result;
    TSet<FName> Seen;
    for(const auto& Slot:Slots)
    {
        const auto* D=Catalog->Find(Slot.ItemId);
        if(!D||!D->IsValidDefinition()||D->bInvisibleAccessory||Seen.Contains(Slot.Slot)||Slot.Slot.IsNone())continue;
        Seen.Add(Slot.Slot);Result.Add({Slot.Slot,D->ItemId,D->Socket,D->Mesh,D->GripTransform});
        if(D->bOccupiesBothHands)
        {auto& Spec=Result.Last();Spec.GripTargetMesh=D->GripTargetMesh.ToSoftObjectPath();Spec.GripMainBone=D->GripMainHandBone;Spec.GripSupportBone=D->GripSupportHandBone;}
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
    if(!Spec.GripTargetMesh.IsNull())
    {
        FTransform Local;
        if(Spec.GripTargetMesh!=FSoftObjectPath(Body->GetSkinnedAsset())||Body->GetBoneIndex(Spec.GripSupportBone)==INDEX_NONE||
            !SocketLocal(Body,Spec.Socket,Spec.GripMainBone,Local)||!AetherEquipmentGrip::IsUniformTransform(Spec.GripTransform)||
            !AetherEquipmentGrip::IsUniformTransform(Body->GetComponentTransform()))return false;
    }
    Visual->SetStaticMesh(Mesh);Visual->SetMobility(EComponentMobility::Movable);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);Visual->SetGenerateOverlapEvents(false);
    Visual->SetCanEverAffectNavigation(false);
    if(Visual->IsRegistered())Visual->AttachToComponent(Body,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Spec.Socket);
    else Visual->SetupAttachment(Body,Spec.Socket);
    // 网格按厘米制作。只继承 Socket 位姿，不继承 FBX 骨架中可能存在的 100 倍单位缩放。
    Visual->SetAbsolute(false,false,true);Visual->SetRelativeTransform(Spec.GripTransform);
    return true;
}
