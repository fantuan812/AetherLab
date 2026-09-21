#include "AetherEquipmentComponent.h"
#include "AetherEquipmentVisuals.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

bool FAetherAttackDefinition::IsValid() const
{
    for (float V : {Damage,PostureDamage,ImpulseNs,CuttingWorkJ,StaminaCost,ReachCm,RadiusCm,WindupSeconds,ActiveSeconds,RecoverySeconds})
        if (!FMath::IsFinite(V) || V<0) return false;
    return !Id.IsNone() && ReachCm>0 && ReachCm<=1000 && RadiusCm>0 && RadiusCm<=200
        && ActiveSeconds>0 && Duration()<=10 && StaminaCost<=100 && Damage<=10000 && CuttingWorkJ<=1000000;
}
const FAetherAttackDefinition* UAetherEquipmentDefinition::FindAttack(FName Id) const
{ return Attacks.FindByPredicate([Id](const auto& A){return A.Id==Id;}); }
bool UAetherEquipmentDefinition::IsValidDefinition() const
{
    if (ItemId.IsNone() || Slot.IsNone() || (!bInvisibleAccessory&&(Socket.IsNone()||Mesh.IsNull())) || GripTransform.ContainsNaN()
        || !FMath::IsFinite(GuardStaminaMultiplier) || GuardStaminaMultiplier<0
        || !FMath::IsFinite(ParryWindowSeconds) || ParryWindowSeconds<0 || ParryWindowSeconds>1) return false;
    if(SupportHandOffset.ContainsNaN()||SupportHandOffset.Size()>100)return false;
    if(bInvisibleAccessory&&(bOccupiesBothHands||bAllowsGuard||!Attacks.IsEmpty()||!SecondarySocket.IsNone()))return false;
    if(!SecondarySocket.IsNone()&&(SecondarySocket==Socket||SecondaryGripTransform.ContainsNaN()))return false;
    TSet<FName> SlotSet;
    for(FName Allowed:AllowedSlots){if(Allowed.IsNone()||SlotSet.Contains(Allowed))return false;SlotSet.Add(Allowed);}
    if(!AllowedSlots.IsEmpty()&&!AllowedSlots.Contains(Slot))return false;
    if(bOccupiesBothHands&&!AllowedSlots.IsEmpty()&&(AllowedSlots.Num()!=1||AllowedSlots[0]!=TEXT("MainHand")))return false;
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
    if (!Catalog || !Catalog->IsValidCatalog() || Loadout.Num()>10) return false;
    TSet<FName> Seen; bool TwoHands=false,OffHand=false;
    for (const auto& S:Loadout)
    {
        const auto* D=Catalog->Find(S.ItemId);
        if (!D || (D->AllowedSlots.IsEmpty()?D->Slot!=S.Slot:!D->AllowedSlots.Contains(S.Slot)) || Seen.Contains(S.Slot)) return false;
        Seen.Add(S.Slot); TwoHands|=D->bOccupiesBothHands; OffHand|=S.Slot==TEXT("OffHand");
    }
    return !(TwoHands&&OffHand);
}
bool UAetherEquipmentComponent::RestoreLoadout(const TArray<FAetherEquippedSlot>& Loadout)
{
    if (!GetOwner()->HasAuthority() || !ValidateLoadout(Loadout)) return false;
    if(Slots==Loadout)return true; // 同一持久快照重发不取消战斗，也不重载全部外观。
    if(LoadoutRevision==MAX_int32)return false;
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
{
    if (GetOwner()->HasAuthority()) return Attack.Serial ? &ActiveDefinition : nullptr;
    auto* D=Catalog?Catalog->Find(Attack.ItemId):nullptr; return D?D->FindAttack(Attack.AttackId):nullptr;
}
bool UAetherEquipmentComponent::IsBusy() const
{
    if (GetOwner()->HasAuthority()) return bAttackRunning;
    const auto* D=CurrentAttack(); return D&&!Attack.bCancelled&&Attack.Phase!=EAetherAttackPhase::Finished&&Clock()<Attack.StartedAt+D->Duration();
}
bool UAetherEquipmentComponent::IsAttackActive() const
{ const auto* D=CurrentAttack(); const float T=Clock()-Attack.StartedAt; return IsBusy()&&D&&T>=D->WindupSeconds&&T<D->WindupSeconds+D->ActiveSeconds; }
bool UAetherEquipmentComponent::CanStartAttack(FName Id) const
{
    if (!GetOwner()->HasAuthority() || IsBusy() || (CanAct.IsBound()&&!CanAct.Execute())) return false;
    const auto* Item=InSlot(TEXT("MainHand")); const auto* D=Item?Item->FindAttack(Id):nullptr;
    return D&&D->IsValid();
}
bool UAetherEquipmentComponent::StartAttack(FName Id)
{ return GetOwner()->HasAuthority()&&RequestAttack.IsBound()&&RequestAttack.Execute(Id); }
bool UAetherEquipmentComponent::BeginCommittedAttack(FName Id,FName ExpectedItem,int32 ExpectedRevision)
{
    if (!CanStartAttack(Id) || LoadoutRevision!=ExpectedRevision) return false;
    auto* Item=InSlot(TEXT("MainHand")); if (!Item||Item->ItemId!=ExpectedItem) return false;
    ActiveDefinition=*Item->FindAttack(Id);
    Attack.ItemId=Item->ItemId; Attack.AttackId=Id; Attack.StartedAt=Clock(); Attack.bCancelled=false;
    if (++Attack.Serial==0) ++Attack.Serial;
    bAttackRunning=true; HitActors.Reset(); LastAttackElapsed=-1; ++AcceptedAttackCount;
    // The caller binds lifecycle delegates before publishing this transition.
    SetAttackPhase(EAetherAttackPhase::Windup); GetOwner()->ForceNetUpdate(); return true;
}
void UAetherEquipmentComponent::SetAttackPhase(EAetherAttackPhase Phase)
{ if (Attack.Phase!=Phase) { Attack.Phase=Phase; OnAttackPhaseChanged.Broadcast(Attack.Serial,Phase); GetOwner()->ForceNetUpdate(); } }
void UAetherEquipmentComponent::FinishAttack(bool Cancelled)
{
    if (!GetOwner()->HasAuthority()||!bAttackRunning) return;
    const uint32 Serial=Attack.Serial;
    bAttackRunning=false; Attack.bCancelled=Cancelled; HitActors.Reset();
    SetAttackPhase(Cancelled?EAetherAttackPhase::Cancelled:EAetherAttackPhase::Finished);
    OnAttackFinished.Broadcast(Serial,Cancelled); GetOwner()->ForceNetUpdate();
}
void UAetherEquipmentComponent::CancelAttack() { FinishAttack(true); }
void UAetherEquipmentComponent::ResolveHits(const FAetherAttackDefinition& D)
{
    const uint32 Serial=Attack.Serial;
    const FVector Start=GetOwner()->GetActorLocation(),Forward=GetOwner()->GetActorForwardVector();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherEquipmentSweep),false,GetOwner());
    FCollisionObjectQueryParams Types; Types.AddObjectTypesToQuery(ECC_Pawn); Types.AddObjectTypesToQuery(ECC_WorldDynamic); Types.AddObjectTypesToQuery(ECC_WorldStatic);
    TArray<FHitResult> Hits;
    GetWorld()->SweepMultiByObjectType(Hits,Start,Start+Forward*D.ReachCm,FQuat::Identity,Types,FCollisionShape::MakeSphere(D.RadiusCm),Q);
    for (const auto& H:Hits)
    {
        // A parry/death callback may cancel this attack during hit dispatch.
        if (!bAttackRunning||Attack.bCancelled||Attack.Serial!=Serial) break;
        AActor* Target=H.GetActor(); if (!Target || Target==GetOwner() || HitActors.Contains(Target) || !Target->Implements<UAetherHitReceiver>()) continue;
        FHitResult Block; const FVector Point=H.bStartPenetrating?Target->GetActorLocation():FVector(H.ImpactPoint);
        if (GetWorld()->LineTraceSingleByChannel(Block,Start,Point,ECC_Visibility,Q) && Block.GetActor()!=Target) continue;
        HitActors.Add(Target); FAetherEquipmentHit Hit; Hit.Source=GetOwner(); Hit.ItemId=Attack.ItemId; Hit.AttackId=Attack.AttackId;
        Hit.Damage=D.Damage; Hit.PostureDamage=D.PostureDamage; Hit.ImpulseNs=Forward*D.ImpulseNs; Hit.CuttingWorkJ=D.CuttingWorkJ;
        ModifyHit.ExecuteIfBound(Hit);
        IAetherHitReceiver::Execute_ReceiveEquipmentHit(Target,Hit); ++AppliedHitCount;
    }
}
void UAetherEquipmentComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* F)
{
    Super::TickComponent(Dt,TickType,F);
    const auto* D=CurrentAttack(); const float Elapsed=Clock()-Attack.StartedAt;
    if (bAttackRunning&&GetOwner()->HasAuthority())
    {
        if (!D || (CanContinueAttack.IsBound()&&!CanContinueAttack.Execute())) CancelAttack();
        else
        {
            const uint32 Serial=Attack.Serial; const FAetherAttackDefinition Definition=*D;
            if (Elapsed>=Definition.WindupSeconds&&LastAttackElapsed<Definition.WindupSeconds+Definition.ActiveSeconds)
            {
                SetAttackPhase(EAetherAttackPhase::Active);
                // Keep one authoritative sample when a frame crosses the complete active window.
                if (bAttackRunning&&Attack.Serial==Serial) ResolveHits(Definition);
            }
            if (bAttackRunning&&Attack.Serial==Serial)
            {
                LastAttackElapsed=Elapsed;
                if (Elapsed>=Definition.Duration()) FinishAttack(false);
                else if (Elapsed>=Definition.WindupSeconds+Definition.ActiveSeconds) SetAttackPhase(EAetherAttackPhase::Recovery);
            }
        }
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
    if(VisualLoad){if(VisualLoad->HasLoadCompleted()){TArray<FSoftObjectPath> Requested;VisualLoad->GetRequestedAssets(Requested);for(const auto& P:Requested)if(!P.ResolveObject()){FailedVisualAssets.Add(P);UE_LOG(LogTemp,Warning,TEXT("Equipment visual unavailable: %s"),*P.ToString());}}VisualLoad->CancelHandle();VisualLoad->ReleaseHandle();VisualLoad.Reset();}
    for (auto& Pair:Visuals) if (Pair.Value) Pair.Value->DestroyComponent(); Visuals.Reset();
    if (!AttachmentTarget || !Catalog || GetNetMode()==NM_DedicatedServer) return;
    TArray<FSoftObjectPath> Missing;for(const auto& S:Slots)if(auto* D=Catalog->Find(S.ItemId);D&&!D->Mesh.IsNull()&&!D->Mesh.Get()&&!FailedVisualAssets.Contains(D->Mesh.ToSoftObjectPath()))Missing.AddUnique(D->Mesh.ToSoftObjectPath());
    if(!Missing.IsEmpty()){VisualLoad=UAssetManager::GetStreamableManager().RequestAsyncLoad(Missing,FStreamableDelegate::CreateUObject(this,&UAetherEquipmentComponent::RebuildVisuals));return;}
    for(const auto& Spec:AetherEquipmentVisuals::Resolve(Catalog,Slots))
    {
        auto* Mesh=Spec.Mesh.Get();if(!Mesh)continue;
        auto* V=NewObject<UStaticMeshComponent>(GetOwner());
        if(!AetherEquipmentVisuals::Attach(AttachmentTarget,V,Spec,Mesh)){V->DestroyComponent();continue;}
        V->RegisterComponent();Visuals.Add(Spec.Slot,V);
    }
}
void UAetherEquipmentComponent::EndPlay(const EEndPlayReason::Type Reason)
{ if(VisualLoad){VisualLoad->CancelHandle();VisualLoad->ReleaseHandle();VisualLoad.Reset();}CancelAttack(); ModifyHit.Unbind(); RequestAttack.Unbind(); CanContinueAttack.Unbind(); for (auto& Pair:Visuals) if (Pair.Value) Pair.Value->DestroyComponent(); Visuals.Reset(); Super::EndPlay(Reason); }
