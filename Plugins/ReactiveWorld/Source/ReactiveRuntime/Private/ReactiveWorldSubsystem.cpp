#include "ReactiveWorldSubsystem.h"
#include "ReactiveBodyComponent.h"
#include "ReactiveMechanismComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/PlatformTime.h"
#include "Components/PrimitiveComponent.h"

namespace
{
bool SharesReactionScope(const UReactiveBodyComponent* A,const UReactiveBodyComponent* B)
{
 auto Allows=[](const UReactiveBodyComponent* Private,const UReactiveBodyComponent* Other)
 {return !Private->bOwnerOnlyStimuli||(Private->GetOwner()->GetOwner()&&(Private->GetOwner()->GetOwner()==Other->GetOwner()||Private->GetOwner()->GetOwner()==Other->GetOwner()->GetOwner()));};
 return Allows(A,B)&&Allows(B,A);
}
}
void UReactiveWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Simulation = MakeUnique<Reactive::FSimulation>();
    Simulation->CanReceiveInput=[this](Reactive::FBodyId Target,Reactive::FBodyId Source)
    {const auto* B=Components.Find(Target);if(!B||!B->IsValid())return false;return !B->Get()->bOwnerOnlyStimuli||Source==Reactive::InvalidBody||B->Get()->GetOwner()->GetOwner()==GetBodyOwner(Source);};
    Simulation->CanExchange = [this](Reactive::FBodyId A, Reactive::FBodyId B) { return CanBodiesExchange(A, B); };
    Simulation->CanConduct = [this](Reactive::FBodyId A, Reactive::FBodyId B)
    {
        auto* AC = Components.Find(A); auto* BC = Components.Find(B);
        if (!AC || !BC || !AC->IsValid() || !BC->IsValid()) return false;
        if(!SharesReactionScope(AC->Get(),BC->Get()))return false;
        auto* AP = AC->Get()->GetPrimitive(); auto* BP = BC->Get()->GetPrimitive();
        if (!AP || !BP || !AP->IsCollisionEnabled() || !BP->IsCollisionEnabled()) return false;
        if (!AP->Bounds.GetBox().ExpandBy(2).Intersect(BP->Bounds.GetBox())) return false;
        FVector OnA, OnB;
        if (AP->GetClosestPointOnCollision(BP->Bounds.Origin, OnA) < 0) return false;
        const float Gap = BP->GetClosestPointOnCollision(OnA, OnB);
        // Actual touching surfaces authorize this edge; a centre-to-centre heat ray
        // may cross another receiver and must not erase a valid contact.
        return Gap >= 0 && Gap <= 2;
    };
}
void UReactiveWorldSubsystem::Deinitialize()
{
    Components.Reset(); Moving.Reset(); Simulation.Reset(); Accumulator = 0;
    Super::Deinitialize();
}
bool UReactiveWorldSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}
bool UReactiveWorldSubsystem::IsAuthority() const { return GetWorld() && GetWorld()->GetNetMode() != NM_Client; }
TStatId UReactiveWorldSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UReactiveWorldSubsystem, STATGROUP_Tickables); }
Reactive::FBodyId UReactiveWorldSubsystem::RegisterBody(UReactiveBodyComponent* Body)
{
    if (!Simulation || !IsAuthority() || !IsValid(Body) || !Body->GetOwner()) return Reactive::InvalidBody;
    const Reactive::FBodyId Id = Simulation->Register(Body->GetMaterial(), Body->GetOwner()->GetActorLocation(),
        Body->InteractionRadiusCm, Body->InitialTemperatureC, Body->InitialWaterKg);
    if (Id != Reactive::InvalidBody)
    {
        Reactive::FElectricalReceiver Receiver;
        if(!Body->ReceiverGroup.IsNone()){auto* Group=ReceiverGroups.Find(Body->ReceiverGroup);Receiver.Id=Group?*Group:ReceiverGroups.Add(Body->ReceiverGroup,NextReceiverGroup++);}
        Receiver.LoadWeight=Body->ReceiverLoad; Receiver.CapacityJ=Body->ReceiverCapacityJ;
        Receiver.HeatFraction=Body->ElectricalHeatFraction; Receiver.bTerminal=Body->bElectricalTerminal;
        Simulation->SetReceiver(Id,Receiver);
        Components.Add(Id, Body); if (Body->bTrackMovement) Moving.Add(Id);
        Body->AcceptState(*Simulation->Find(Id));
    }
    else UE_LOG(LogTemp, Error, TEXT("Reactive registration rejected: %s; check material, radius and water capacity."), *GetNameSafe(Body->GetOwner()));
    return Id;
}
void UReactiveWorldSubsystem::UnregisterBody(Reactive::FBodyId Id)
{
    Components.Remove(Id); Moving.Remove(Id); if (Simulation) Simulation->Unregister(Id);
}
bool UReactiveWorldSubsystem::Submit(UReactiveBodyComponent* Target, const FReactiveStimulus& Input)
{
    if (!Simulation || !IsAuthority()) return false;
    if (Target && (!IsValid(Target) || Target->GetWorld() != GetWorld() || Target->GetBodyId() == Reactive::InvalidBody)) return false;
    if(Target&&Target->bOwnerOnlyStimuli&&Input.SourceActor&&Target->GetOwner()->GetOwner()!=Input.SourceActor)return false;
    Reactive::FStimulus S; S.InputId=Input.InputId;S.RootCauseId=Input.RootCauseId; S.Target = Target ? Target->GetBodyId() : Reactive::InvalidBody;
    if (IsValid(Input.SourceActor) && Input.SourceActor->GetWorld() == GetWorld())
        if (auto* Source = Input.SourceActor->FindComponentByClass<UReactiveBodyComponent>()) S.Source = Source->GetBodyId();
    S.PositionCm = Input.PositionCm; S.RadiusCm = Input.RadiusCm; S.HeatJ = Input.HeatJ;
    S.WaterKg = Input.WaterKg; S.ElectricalJ = Input.ElectricalJ; S.ImpulseNs = Input.ImpulseNs;S.bApplyPhysicsImpulse=Input.bApplyPhysicsImpulse;S.CuttingWorkJ=Input.CuttingWorkJ;
    return Simulation->Enqueue(S);
}
bool UReactiveWorldSubsystem::SetWeather(double T, double Rain, FVector Wind)
{
    if (!Simulation || !IsAuthority()) return false;
    Reactive::FEnvironment E; E.TemperatureC = T; E.RainKgPerM2Sec = Rain; E.WindMPerSec = Wind;
    return Simulation->SetEnvironment(E);
}
bool UReactiveWorldSubsystem::CanBodiesExchange(Reactive::FBodyId A, Reactive::FBodyId B) const
{
    const uint64 Key = (uint64(FMath::Min(A, B)) << 32) | FMath::Max(A, B);
    if (const bool* Cached = ContactCache.Find(Key))
    { ++const_cast<UReactiveWorldSubsystem*>(this)->OcclusionCacheHits; return *Cached; }
    const auto* AP = Components.Find(A); const auto* BP = Components.Find(B);
    const UReactiveBodyComponent* AC = AP ? AP->Get() : nullptr;
    const UReactiveBodyComponent* BC = BP ? BP->Get() : nullptr;
    if (!IsValid(AC) || !IsValid(BC) || !GetWorld()) return false;
    if(!SharesReactionScope(AC,BC))return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ReactiveExchange), false);
    Params.AddIgnoredActor(AC->GetOwner()); Params.AddIgnoredActor(BC->GetOwner());
    // Reaction-blocking geometry must block Visibility. This gate is a prototype approximation.
    const bool Clear = !GetWorld()->LineTraceTestByChannel(AC->GetOwner()->GetActorLocation(), BC->GetOwner()->GetActorLocation(), ECC_Visibility, Params);
    ++const_cast<UReactiveWorldSubsystem*>(this)->OcclusionTraces;
    ContactCache.Add(Key, Clear); return Clear;
}
void UReactiveWorldSubsystem::Tick(float Dt)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ReactiveWorld);
    if (!Simulation || !IsAuthority() || !GetWorld()->HasBegunPlay() || Dt <= 0 || !FMath::IsFinite(Dt)) return;
    const double Step = Simulation->GetSettings().StepSeconds;
    const double Total = Accumulator + Dt;
    Accumulator = FMath::Min(Total, 4 * Step); DroppedSeconds += Total - Accumulator;
    if (Accumulator < Step) return;
    TArray<Reactive::FBodyId> Dead;
    for (Reactive::FBodyId Id : Moving)
    {
        const auto* Entry = Components.Find(Id); UReactiveBodyComponent* Body = Entry ? Entry->Get() : nullptr;
        if (IsValid(Body)) Simulation->Move(Id, Body->GetOwner()->GetActorLocation()); else Dead.Add(Id);
    }
    for (Reactive::FBodyId Id : Dead) UnregisterBody(Id);
    while (Accumulator + 1.e-9 >= Step)
    {
        ContactCache.Reset(); OcclusionTraces = 0; OcclusionCacheHits = 0;
        const double Started = FPlatformTime::Seconds();
        Simulation->Step(); Accumulator -= Step;
        LastStepMilliseconds = (FPlatformTime::Seconds() - Started) * 1000;
        for (Reactive::FBodyId Id : Simulation->GetChangedBodies())
        {
            const auto* Entry = Components.Find(Id); UReactiveBodyComponent* Body = Entry ? Entry->Get() : nullptr;
            if (IsValid(Body)) if (const Reactive::FState* S = Simulation->Find(Id)) Body->AcceptState(*S);
        }
        const TArray<Reactive::FEvent> Events = Simulation->DrainEvents();
        for (const Reactive::FEvent& E : Events)
        {
            const auto* Entry = Components.Find(E.Body); UReactiveBodyComponent* Body = Entry ? Entry->Get() : nullptr;
            if (IsValid(Body)) Body->AcceptEvent(E);
        }
    }
}
FString UReactiveWorldSubsystem::GetStatsText() const
{
    if (!Simulation) return TEXT("Reactive world offline");
    const auto& S = Simulation->GetStats();
    return FString::Printf(TEXT("Bodies %d | Active %d | Heat pairs %d | Electric visits %d | Step %llu | Rejected %llu | Budget %llu | Dropped %.3fs"),
        S.Registered, S.Active, S.ThermalPairs, S.ElectricalVisits, S.Steps, S.RejectedInputs, S.BudgetHits, DroppedSeconds);
}

AActor* UReactiveWorldSubsystem::GetBodyOwner(Reactive::FBodyId Id) const
{
    const auto* Entry = Components.Find(Id);
    return Entry && Entry->IsValid() ? Entry->Get()->GetOwner() : nullptr;
}
double UReactiveWorldSubsystem::TransferWater(UReactiveBodyComponent* From, UReactiveBodyComponent* To, double MaxKg, AActor* SourceActor)
{
    if (!IsAuthority() || !Simulation || !IsValid(From) || !IsValid(To) || From->GetWorld() != GetWorld() || To->GetWorld() != GetWorld()) return 0;
    Reactive::FBodyId Source = Reactive::InvalidBody;
    if (SourceActor)
    {
        if (!IsValid(SourceActor) || SourceActor->GetWorld() != GetWorld()) return 0;
        const auto* Body = SourceActor->FindComponentByClass<UReactiveBodyComponent>();
        if (!Body || Body->GetBodyId() == Reactive::InvalidBody) return 0;
        Source = Body->GetBodyId();
    }
    ContactCache.Reset();
    const double Moved = Simulation->TransferLiquid(From->GetBodyId(), To->GetBodyId(), MaxKg, Source);
    if (Moved > 0) { From->AcceptState(*Simulation->Find(From->GetBodyId())); To->AcceptState(*Simulation->Find(To->GetBodyId())); }
    return Moved;
}
namespace
{
uint32 MaterialSignature(const Reactive::FMaterial& M, int32 Schema)
{
    const double Values[] = { M.DryMassKg, M.SpecificHeatJPerKgK, M.WaterCapacityKg, M.InitialFuelKg, M.IgnitionC,
        M.BurnRateKgPerSec, M.CombustionJPerKg, M.RetainedHeatFraction, M.Conductivity, M.ThermalCouplingWPerK,
        M.CoolingWPerK, M.StrengthNs, M.FrozenStrengthMultiplier, M.SealedVolumeM3, M.BurstGaugePressurePa };
    uint32 Hash = GetTypeHash(M.bLiquidConductor);
    for (double V : Values) Hash = HashCombineFast(Hash, GetTypeHash(V));
    return Schema == 1 ? HashCombineFast(Hash, GetTypeHash(M.CutResistanceJ)) : Hash;
}
}
double UReactiveWorldSubsystem::WithdrawWater(UReactiveBodyComponent* From, double MaxKg)
{
    if (!IsAuthority() || !Simulation || !IsValid(From) || From->GetWorld() != GetWorld()) return 0;
    const double Taken=Simulation->WithdrawLiquid(From->GetBodyId(), MaxKg);
    if(Taken>0)From->AcceptState(*Simulation->Find(From->GetBodyId()));
    return Taken;
}
bool UReactiveWorldSubsystem::Capture(TArray<FReactiveSaveRecord>& Records) const
{
    Records.Reset(); if (!IsAuthority() || !Simulation || Simulation->HasPendingInputs()) return false;
    TSet<FName> Names;
    for (const auto& Pair : Components)
    {
        const UReactiveBodyComponent* Body = Pair.Value.Get();
        if (!IsValid(Body) || Body->StableId.IsNone()) continue;
        if (Names.Contains(Body->StableId)) { Records.Reset(); return false; }
        Names.Add(Body->StableId);
        const Reactive::FState* S = Simulation->Find(Pair.Key); if (!S) return false;
        FReactiveSaveRecord R; R.StableId = Body->StableId; R.Transform = Body->GetOwner()->GetActorTransform();
        R.MaterialSchema = 1; R.MaterialSignature = MaterialSignature(Body->GetMaterial(), R.MaterialSchema);
        R.ElectricalWaterKg=S->ElectricalWaterKg; R.ElectricalWetness01 = S->ElectricalWetness01; R.EnthalpyJ = S->EnthalpyJ; R.WaterKg = S->WaterKg; R.FuelKg = S->FuelKg; R.Integrity = S->Integrity;
        R.GasEnergyJ = S->GasEnergyJ; R.bBurning = S->bBurning; R.bBroken = S->bBroken; R.bBurst = S->bBurst;
        if(auto* M=Body->GetOwner()->FindComponentByClass<UReactiveMechanismComponent>())
        {R.bGateOpen=M->bGateOpen;R.bHasMechanism=true;R.bSupportReleased=M->bReleased;R.bSourceEnabled=M->bPowerEnabled;R.RemainingEnergyJ=M->RemainingEnergyJ;R.SourceAge=M->SourceAge;}
        Records.Add(R);
    }
    Records.Sort([](const FReactiveSaveRecord& A, const FReactiveSaveRecord& B) { return A.StableId.LexicalLess(B.StableId); });
    return true;
}
bool UReactiveWorldSubsystem::Restore(const TArray<FReactiveSaveRecord>& Records)
{
    if (!IsAuthority() || !Simulation) return false;
    TMap<FName, UReactiveBodyComponent*> ByName;
    for (const auto& Pair : Components)
        if (UReactiveBodyComponent* B = Pair.Value.Get(); IsValid(B) && !B->StableId.IsNone())
        { if (ByName.Contains(B->StableId)) return false; ByName.Add(B->StableId, B); }
    if (Records.Num() != ByName.Num()) return false;
    TSet<FName> Seen;
    TMap<Reactive::FBodyId, Reactive::FState> States;
    for (const FReactiveSaveRecord& R : Records)
    {
        UReactiveBodyComponent* B = ByName.FindRef(R.StableId);
        if (!B || Seen.Contains(R.StableId) || (R.MaterialSchema != 0 && R.MaterialSchema != 1) || R.MaterialSignature != MaterialSignature(B->GetMaterial(), R.MaterialSchema)
            || !R.Transform.IsValid() || R.Transform.GetLocation().GetAbsMax() > 1.e8
            || R.Transform.GetScale3D().GetMin() <= 0 || R.Transform.GetScale3D().GetMax() > 1000) return false;
        if(!FMath::IsFinite(R.RemainingEnergyJ)||R.RemainingEnergyJ<0||!FMath::IsFinite(R.SourceAge)||R.SourceAge<0)return false;
        Seen.Add(R.StableId);
        Reactive::FState S; S.ElectricalWaterKg=R.ElectricalWaterKg; S.ElectricalWetness01 = R.ElectricalWetness01; S.EnthalpyJ = R.EnthalpyJ; S.WaterKg = R.WaterKg; S.FuelKg = R.FuelKg; S.Integrity = R.Integrity;
        S.GasEnergyJ = R.GasEnergyJ; S.bBurning = R.bBurning; S.bBroken = R.bBroken; S.bBurst = R.bBurst;
        States.Add(B->GetBodyId(), S);
    }
    if (!Simulation->RestoreStates(States)) return false;
    ContactCache.Reset(); Accumulator = 0;
    for (const FReactiveSaveRecord& R : Records)
    {
        UReactiveBodyComponent* B = ByName[R.StableId];
        if (UPrimitiveComponent* P = B->GetPrimitive()) { P->SetSimulatePhysics(false); }
        B->GetOwner()->SetActorTransform(R.Transform, false, nullptr, ETeleportType::TeleportPhysics);
        Simulation->Move(B->GetBodyId(), R.Transform.GetLocation());
        B->AcceptState(*Simulation->Find(B->GetBodyId()));
        if(R.bHasMechanism)if(auto* M=B->GetOwner()->FindComponentByClass<UReactiveMechanismComponent>()){M->bGateOpen=R.bGateOpen;M->RestoreMechanism(R.bSupportReleased,R.RemainingEnergyJ,R.SourceAge,R.bSourceEnabled);}
        B->GetOwner()->ForceNetUpdate();
    }
    return true;
}
