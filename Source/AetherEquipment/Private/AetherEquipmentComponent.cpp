#include "AetherEquipmentComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

bool FAetherAttackDefinition::IsValid() const
{
    for (float V : {Damage,PostureDamage,ImpulseNs,StaminaCost,ReachCm,RadiusCm,WindupSeconds,ActiveSeconds,RecoverySeconds})
        if (!FMath::IsFinite(V) || V<0) return false;
    return !Id.IsNone() && ReachCm>0 && ReachCm<=1000 && RadiusCm>0 && RadiusCm<=200
        && ActiveSeconds>0 && Duration()<=10 && StaminaCost<=100 && Damage<=10000;
}
const FAetherAttackDefinition* UAetherEquipmentDefinition::FindAttack(FName Id) const
{ return Attacks.FindByPredicate([Id](const auto& A){return A.Id==Id;}); }
bool UAetherEquipmentDefinition::IsValidDefinition() const
{
    if (ItemId.IsNone() || Slot.IsNone() || Socket.IsNone() || Mesh.IsNull() || GripTransform.ContainsNaN()
        || !FMath::IsFinite(GuardStaminaMultiplier) || GuardStaminaMultiplier<0
        || !FMath::IsFinite(ParryWindowSeconds) || ParryWindowSeconds<0 || ParryWindowSeconds>1) return false;
    TSet<FName> Seen;
    for (const auto& A:Attacks) { if (!A.IsValid() || Seen.Contains(A.Id)) return false; Seen.Add(A.Id); }
    return !bOccupiesBothHands || Slot==TEXT("MainHand");
}
UAetherEquipmentDefinition* UAetherEquipmentCatalog::Find(FName Id) const
{ for (const auto& Item:Items) if (Item && Item->ItemId==Id) return Item; return nullptr; }
bool UAetherEquipmentCatalog::IsValidCatalog() const
{
    TSet<FName> Seen;
    for (const auto& Item:Items) { if (!Item || !Item->IsValidDefinition() || Seen.Contains(Item->ItemId)) return false; Seen.Add(Item->ItemId); }
    return Items.Num()>0;
}
UAetherEquipmentComponent::UAetherEquipmentComponent()
{ PrimaryComponentTick.bCanEverTick=true; SetIsReplicatedByDefault(true); }
void UAetherEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UAetherEquipmentComponent,Catalog); DOREPLIFETIME(UAetherEquipmentComponent,Slots);
    DOREPLIFETIME(UAetherEquipmentComponent,LoadoutRevision); DOREPLIFETIME(UAetherEquipmentComponent,Attack);
}
float UAetherEquipmentComponent::Clock() const
{ const auto* GS=GetWorld()->GetGameState(); return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds(); }
UAetherEquipmentDefinition* UAetherEquipmentComponent::InSlot(FName Slot) const
{ if (Catalog) for (const auto& S:Slots) if (S.Slot==Slot) return Catalog->Find(S.ItemId); return nullptr; }
UAetherEquipmentDefinition* UAetherEquipmentComponent::GuardDefinition() const
{ if (Catalog) for (const auto& S:Slots) if (auto* D=Catalog->Find(S.ItemId); D && D->bAllowsGuard) return D; return nullptr; }
bool UAetherEquipmentComponent::ValidateLoadout(const TArray<FAetherEquippedSlot>& Loadout) const
{
    if (!Catalog || !Catalog->IsValidCatalog() || Loadout.Num()>8) return false;
    TSet<FName> Seen; bool TwoHands=false,OffHand=false;
    for (const auto& S:Loadout)
    {
        const auto* D=Catalog->Find(S.ItemId);
        if (!D || D->Slot!=S.Slot || Seen.Contains(S.Slot)) return false;
        Seen.Add(S.Slot); TwoHands|=D->bOccupiesBothHands; OffHand|=S.Slot==TEXT("OffHand");
    }
    return !(TwoHands&&OffHand);
}
bool UAetherEquipmentComponent::RestoreLoadout(const TArray<FAetherEquippedSlot>& Loadout)
{
    if (!GetOwner()->HasAuthority() || !ValidateLoadout(Loadout)) return false;
    CancelAttack(); Slots=Loadout; ++LoadoutRevision; RebuildVisuals(); OnLoadoutChanged.Broadcast(); GetOwner()->ForceNetUpdate(); return true;
}
bool UAetherEquipmentComponent::Equip(FName ItemId)
{
    if (!GetOwner()->HasAuthority()) { ServerEquip(ItemId); return false; }
    if (IsBusy() || (CanAct.IsBound()&&!CanAct.Execute()) || !Catalog) return false;
    const auto* D=Catalog->Find(ItemId); if (!D || (OwnsItem.IsBound() && !OwnsItem.Execute(ItemId))) return false;
    if (const auto* Current=InSlot(D->Slot); Current && Current->ItemId==ItemId) return true;
    TArray<FAetherEquippedSlot> Next=Slots;
    Next.RemoveAll([D](const auto& S){ return S.Slot==D->Slot || (D->bOccupiesBothHands&&S.Slot==TEXT("OffHand")); });
    FAetherEquippedSlot S; S.Slot=D->Slot; S.ItemId=ItemId; Next.Add(S);
    return RestoreLoadout(Next);
}
void UAetherEquipmentComponent::ServerEquip_Implementation(FName ItemId) { if (!bProfileManaged) Equip(ItemId); }
bool UAetherEquipmentComponent::Unequip(FName Slot)
{
    if (!GetOwner()->HasAuthority()) { ServerUnequip(Slot); return false; }
    if (IsBusy() || (CanAct.IsBound()&&!CanAct.Execute())) return false;
    auto Next=Slots; if (!Next.RemoveAll([Slot](const auto& S){return S.Slot==Slot;})) return false;
    return RestoreLoadout(Next);
}
void UAetherEquipmentComponent::ServerUnequip_Implementation(FName Slot) { if (!bProfileManaged) Unequip(Slot); }
const FAetherAttackDefinition* UAetherEquipmentComponent::CurrentAttack() const
{ auto* D=Catalog?Catalog->Find(Attack.ItemId):nullptr; return D?D->FindAttack(Attack.AttackId):nullptr; }
bool UAetherEquipmentComponent::IsBusy() const
{ const auto* D=CurrentAttack(); return D&&!Attack.bCancelled&&Clock()<Attack.StartedAt+D->Duration(); }
bool UAetherEquipmentComponent::IsAttackActive() const
{ const auto* D=CurrentAttack(); const float T=Clock()-Attack.StartedAt; return D&&!Attack.bCancelled&&T>=D->WindupSeconds&&T<D->WindupSeconds+D->ActiveSeconds; }
bool UAetherEquipmentComponent::StartAttack(FName Id)
{
    if (!GetOwner()->HasAuthority() || IsBusy() || (CanAct.IsBound()&&!CanAct.Execute())) return false;
    auto* Item=InSlot(TEXT("MainHand")); const auto* D=Item?Item->FindAttack(Id):nullptr;
    auto* ASC=UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
    if (!D || !D->IsValid() || !ASC || !StaminaAttribute.IsValid() || ASC->GetNumericAttribute(StaminaAttribute)<D->StaminaCost) return false;
    ASC->ApplyModToAttribute(StaminaAttribute,EGameplayModOp::Additive,-D->StaminaCost);
    Attack.ItemId=Item->ItemId; Attack.AttackId=Id; Attack.StartedAt=Clock(); Attack.bCancelled=false; ++Attack.Serial;
    HitActors.Reset(); LastAttackElapsed=-1; ++AcceptedAttackCount; GetOwner()->ForceNetUpdate(); return true;
}
void UAetherEquipmentComponent::CancelAttack()
{ if (GetOwner()->HasAuthority() && !Attack.bCancelled) { Attack.bCancelled=true; HitActors.Reset(); GetOwner()->ForceNetUpdate(); } }
void UAetherEquipmentComponent::ResolveHits(const FAetherAttackDefinition& D)
{
    const FVector Start=GetOwner()->GetActorLocation(),Forward=GetOwner()->GetActorForwardVector();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherEquipmentSweep),false,GetOwner());
    FCollisionObjectQueryParams Types; Types.AddObjectTypesToQuery(ECC_Pawn); Types.AddObjectTypesToQuery(ECC_WorldDynamic); Types.AddObjectTypesToQuery(ECC_WorldStatic);
    TArray<FHitResult> Hits;
    GetWorld()->SweepMultiByObjectType(Hits,Start,Start+Forward*D.ReachCm,FQuat::Identity,Types,FCollisionShape::MakeSphere(D.RadiusCm),Q);
    for (const auto& H:Hits)
    {
        // A parry/death callback may cancel this attack during hit dispatch.
        if (Attack.bCancelled) break;
        AActor* Target=H.GetActor(); if (!Target || Target==GetOwner() || HitActors.Contains(Target) || !Target->Implements<UAetherHitReceiver>()) continue;
        FHitResult Block; const FVector Point=H.bStartPenetrating?Target->GetActorLocation():FVector(H.ImpactPoint);
        if (GetWorld()->LineTraceSingleByChannel(Block,Start,Point,ECC_Visibility,Q) && Block.GetActor()!=Target) continue;
        HitActors.Add(Target); FAetherEquipmentHit Hit; Hit.Source=GetOwner(); Hit.ItemId=Attack.ItemId; Hit.AttackId=Attack.AttackId;
        Hit.Damage=D.Damage; Hit.PostureDamage=D.PostureDamage; Hit.ImpulseNs=Forward*D.ImpulseNs;
        IAetherHitReceiver::Execute_ReceiveEquipmentHit(Target,Hit); ++AppliedHitCount;
    }
}
void UAetherEquipmentComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* F)
{
    Super::TickComponent(Dt,TickType,F);
    const auto* D=CurrentAttack(); const float Elapsed=Clock()-Attack.StartedAt;
    if (D&&!Attack.bCancelled&&GetOwner()->HasAuthority())
    {
        // A hitch crossing the complete active interval still resolves one authoritative sample.
        if (Elapsed>=D->WindupSeconds && LastAttackElapsed<D->WindupSeconds+D->ActiveSeconds) ResolveHits(*D);
        LastAttackElapsed=Elapsed;
    }
    if (auto* Visual=VisualForSlot(TEXT("MainHand")))
    {
        auto* Item=InSlot(TEXT("MainHand")); FTransform T=Item?Item->GripTransform:FTransform::Identity;
        if (D&&IsBusy())
        {
            const float Alpha=FMath::Clamp((Elapsed-D->WindupSeconds)/D->ActiveSeconds,0.f,1.f);
            const float Angle=Elapsed<D->WindupSeconds?-20.f:Elapsed<D->WindupSeconds+D->ActiveSeconds?FMath::Lerp(-20.f,70.f,Alpha):70.f*(1-FMath::Clamp((Elapsed-D->WindupSeconds-D->ActiveSeconds)/FMath::Max(.01f,D->RecoverySeconds),0.f,1.f));
            T.ConcatenateRotation(FQuat(FVector::RightVector,FMath::DegreesToRadians(Angle)));
        }
        Visual->SetRelativeTransform(T);
    }
}
void UAetherEquipmentComponent::SetAttachmentTarget(USkinnedMeshComponent* Mesh) { AttachmentTarget=Mesh; RebuildVisuals(); }
UStaticMeshComponent* UAetherEquipmentComponent::VisualForSlot(FName Slot) const
{ const auto* Found=Visuals.Find(Slot); return Found?Found->Get():nullptr; }
void UAetherEquipmentComponent::OnRep_Loadout() { RebuildVisuals(); OnLoadoutChanged.Broadcast(); }
void UAetherEquipmentComponent::RebuildVisuals()
{
    for (auto& Pair:Visuals) if (Pair.Value) Pair.Value->DestroyComponent(); Visuals.Reset();
    if (!AttachmentTarget || !Catalog || GetNetMode()==NM_DedicatedServer) return;
    for (const auto& S:Slots)
    {
        auto* D=Catalog->Find(S.ItemId); if (!D || !AttachmentTarget->DoesSocketExist(D->Socket)) continue;
        auto* Mesh=D->Mesh.LoadSynchronous(); if (!Mesh) continue;
        auto* V=NewObject<UStaticMeshComponent>(GetOwner()); V->SetStaticMesh(Mesh); V->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        V->SetGenerateOverlapEvents(false); V->SetupAttachment(AttachmentTarget,D->Socket);
        // Equipment meshes are already imported in centimetres. FBX skeletons
        // can carry a 100x root-unit scale; inherit the hand pose, not that scale.
        V->SetAbsolute(false,false,true); V->RegisterComponent(); V->SetRelativeTransform(D->GripTransform); Visuals.Add(S.Slot,V);
    }
}
void UAetherEquipmentComponent::EndPlay(const EEndPlayReason::Type Reason)
{ for (auto& Pair:Visuals) if (Pair.Value) Pair.Value->DestroyComponent(); Visuals.Reset(); Super::EndPlay(Reason); }
