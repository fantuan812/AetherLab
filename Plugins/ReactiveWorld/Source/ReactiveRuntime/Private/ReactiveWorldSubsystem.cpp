#include "ReactiveWorldSubsystem.h"
#include "ReactiveBodyComponent.h"
#include "ReactiveMechanismComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
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
    Simulation->CanConduct = [this](Reactive::FBodyId A,Reactive::FBodyId B)
    {
        const uint64 Key=(uint64(FMath::Min(A,B))<<32)|FMath::Max(A,B);
        const auto* Contact=ElectricalContacts.Find(Key);
        return Contact&&Contact->Record.bValid&&Contact->Record.LastConfirmedStep==Simulation->GetStats().Steps+1;
    };
    Simulation->ElectricalNeighbors=[this](Reactive::FBodyId A){return ElectricalAdjacency.FindRef(A);};
    Simulation->ConductContact=[this](Reactive::FBodyId A,Reactive::FBodyId B)
    {
        Reactive::FContactRef Ref;const uint64 Key=(uint64(FMath::Min(A,B))<<32)|FMath::Max(A,B);
        if(const auto* C=ElectricalContacts.Find(Key))
        {Ref.A=C->Record.BodyA;Ref.B=C->Record.BodyB;Ref.Version=C->Record.Version;Ref.ConfirmedStep=C->Record.LastConfirmedStep;Ref.PositionCm=C->Record.PositionCm;Ref.bValid=C->Record.bValid;}
        return Ref;
    };
    Simulation->CanTransferLiquid=[this](Reactive::FBodyId A,Reactive::FBodyId B){return CanBodiesTransferLiquid(A,B);};
}
void UReactiveWorldSubsystem::Deinitialize()
{
    Components.Reset(); Moving.Reset(); ElectricalContacts.Reset();ElectricalAdjacency.Reset();ThermalContacts.Reset();LiquidContacts.Reset();ReceiverGroups.Reset();ElectricalRecipients.Reset(); Simulation.Reset(); Accumulator = 0;
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
    for(auto It=ElectricalRecipients.CreateIterator();It;++It)if(!It.Value().IsValid()||It.Value()->GetBodyId()==Id)It.RemoveCurrent();
    ElectricalAdjacency.Remove(Id);for(auto& Pair:ElectricalAdjacency)Pair.Value.Remove(Id);
    Components.Remove(Id); Moving.Remove(Id); if (Simulation) Simulation->Unregister(Id);
    for(auto It=ElectricalContacts.CreateIterator();It;++It)if(It.Value().Record.BodyA==Id||It.Value().Record.BodyB==Id)It.RemoveCurrent();
    for(auto* Records:{&ThermalContacts,&LiquidContacts})for(auto It=Records->CreateIterator();It;++It)if(It.Value().BodyA==Id||It.Value().BodyB==Id)It.RemoveCurrent();
    ContactCache.Reset();
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
    if(ThermalContacts.Num()<16384)
    {
        auto& Contact=ThermalContacts.FindOrAdd(Key);Contact.A=ContactBodyName(A);Contact.B=ContactBodyName(B);Contact.BodyA=A;Contact.BodyB=B;
        Contact.Channel=EReactiveContactChannel::Thermal;Contact.PositionCm=(AC->GetOwner()->GetActorLocation()+BC->GetOwner()->GetActorLocation())*.5;
        Contact.bValid=Clear;Contact.LastConfirmedStep=Simulation->GetStats().Steps+1;Contact.Version=Contact.LastConfirmedStep;
    }
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
        ContactCache.Reset();ThermalContacts.Reset(); OcclusionTraces = 0; OcclusionCacheHits = 0;
        const uint64 StepId=Simulation->GetStats().Steps+1;
        const uint64 ContactBudgetBefore=ContactBudgetHits;
        AdvanceLiquidPorts(Step,StepId);
        UpdateElectricalContacts(StepId);
        TArray<Reactive::FBodyId> Ordered;Components.GetKeys(Ordered);Ordered.Sort();
        for(auto Id:Ordered)if(auto* Body=Components.FindRef(Id).Get())
            if(auto* Source=Body->GetOwner()->FindComponentByClass<UReactiveMechanismComponent>())Source->AdvancePower(Step);
        const uint64 RejectedBefore=Simulation->GetStats().RejectedElectricalPulses;
        const double Started = FPlatformTime::Seconds();
        Simulation->Step(); Accumulator -= Step;
        LastStepMilliseconds = (FPlatformTime::Seconds() - Started) * 1000;
        for (Reactive::FBodyId Id : Simulation->GetChangedBodies())
        {
            const auto* Entry = Components.Find(Id); UReactiveBodyComponent* Body = Entry ? Entry->Get() : nullptr;
            if (IsValid(Body)) if (const Reactive::FState* S = Simulation->Find(Id)) Body->AcceptState(*S);
        }
        PublishElectricalWindows();
        if((Simulation->GetStats().RejectedElectricalPulses>RejectedBefore||ContactBudgetHits>ContactBudgetBefore)&&GetWorld()->GetTimeSeconds()-LastBudgetWarningAt>=5)
        {LastBudgetWarningAt=GetWorld()->GetTimeSeconds();UE_LOG(LogTemp,Warning,TEXT("Reactive budget refusal: electricPulses=%llu lostJ=%.3f contactCandidates=%llu"),Simulation->GetStats().RejectedElectricalPulses,Simulation->GetStats().ElectricalLostJ,ContactBudgetHits);}
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
    return FString::Printf(TEXT("Bodies %d | Active %d | Heat pairs %d | Electric visits %d | Step %llu | Rejected %llu | Budget %llu | Dropped %.3fs | ContactLimit %llu | ElectricRejected %llu | LostJ %.2f"),
        S.Registered, S.Active, S.ThermalPairs, S.ElectricalVisits, S.Steps, S.RejectedInputs, S.BudgetHits, DroppedSeconds,ContactBudgetHits,S.RejectedElectricalPulses,S.ElectricalLostJ);
}

AActor* UReactiveWorldSubsystem::GetBodyOwner(Reactive::FBodyId Id) const
{
    const auto* Entry = Components.Find(Id);
    return Entry && Entry->IsValid() ? Entry->Get()->GetOwner() : nullptr;
}
double UReactiveWorldSubsystem::TransferWater(UReactiveBodyComponent* From, UReactiveBodyComponent* To, double MaxKg, AActor* SourceActor)
{
    if (!IsAuthority() || !Simulation || !IsValid(From) || !IsValid(To) || From->GetWorld() != GetWorld() || To->GetWorld() != GetWorld()) return 0;
    if(!SourceActor&&!SharesReactionScope(From,To))return 0;
    if(SourceActor&&((From->bOwnerOnlyStimuli&&From->GetOwner()->GetOwner()!=SourceActor)||(To->bOwnerOnlyStimuli&&To->GetOwner()->GetOwner()!=SourceActor)))return 0;
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
    ContactCache.Reset();ElectricalContacts.Reset();ElectricalAdjacency.Reset();ThermalContacts.Reset();LiquidContacts.Reset();ElectricalRecipients.Reset(); Accumulator = 0;
    for(const auto& Pair:Components)if(auto* B=Pair.Value.Get())B->ResetElectricalWindow();
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

FName UReactiveWorldSubsystem::ContactBodyName(Reactive::FBodyId Id) const
{
    const auto* B=Components.Find(Id);
    if(B&&B->IsValid()&&!B->Get()->StableId.IsNone())return B->Get()->StableId;
    return Id?FName(*FString::Printf(TEXT("Runtime_%u"),Id)):NAME_None;
}
TArray<FReactiveContact> UReactiveWorldSubsystem::GetContacts() const
{
    TArray<FReactiveContact> Result;
    for(const auto& P:ElectricalContacts)Result.Add(P.Value.Record);
    for(const auto& P:ThermalContacts)Result.Add(P.Value);
    for(const auto& P:LiquidContacts)Result.Add(P.Value);
    Result.Sort([](const auto& A,const auto& B){if(A.Channel!=B.Channel)return uint8(A.Channel)<uint8(B.Channel);return A.BodyA!=B.BodyA?A.BodyA<B.BodyA:A.BodyB<B.BodyB;});
    return Result;
}
void UReactiveWorldSubsystem::UpdateElectricalContacts(uint64 StepId)
{
    const double Enter=FMath::IsFinite(ContactEnterCm)?FMath::Clamp(ContactEnterCm,0.,6.):2.;
    const double Exit=FMath::IsFinite(ContactExitCm)?FMath::Clamp(ContactExitCm,Enter,6.):6.;
    const uint32 Confirm=FMath::Clamp(ContactConfirmSteps,1u,8u);
    for(auto It=ElectricalContacts.CreateIterator();It;++It)if(It.Value().LastObservedStep+1<StepId)It.RemoveCurrent();
    TArray<Reactive::FBodyId> IDs;Components.GetKeys(IDs);IDs.Sort();
    ElectricalAdjacency.Reset();
    for(auto A:IDs)
    {
        auto* AC=Components.FindRef(A).Get();if(!IsValid(AC))continue;
        auto* Shape=AC->GetPrimitive();if(!Shape||!Shape->IsRegistered())continue;
        // Use actual collision bounds, not the thermal interaction radius (long rods and water tiles differ).
        FCollisionObjectQueryParams Types;Types.AddObjectTypesToQuery(ECC_WorldStatic);Types.AddObjectTypesToQuery(ECC_WorldDynamic);Types.AddObjectTypesToQuery(ECC_Pawn);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ReactiveContactCandidates),false,AC->GetOwner());
        TArray<FOverlapResult> Overlaps;TSet<Reactive::FBodyId> Candidates;
        GetWorld()->OverlapMultiByObjectType(Overlaps,Shape->Bounds.Origin,FQuat::Identity,Types,FCollisionShape::MakeBox(Shape->Bounds.BoxExtent+FVector(Exit)),Query);
        for(const auto& Hit:Overlaps)if(auto* Owner=Hit.GetActor())if(auto* Body=Owner->FindComponentByClass<UReactiveBodyComponent>())if(Body->GetBodyId()!=Reactive::InvalidBody)Candidates.Add(Body->GetBodyId());
        TArray<Reactive::FBodyId> Ordered=Candidates.Array();Ordered.Sort();
        for(auto B:Ordered)
        {
            if(B<=A)continue;auto* BC=Components.FindRef(B).Get();if(!IsValid(BC))continue;
            auto* AP=AC->GetPrimitive();auto* BP=BC->GetPrimitive();
            const uint64 Key=(uint64(A)<<32)|B;
            auto* Existing=ElectricalContacts.Find(Key);
            if(!Existing&&(!AP||!BP||!AP->Bounds.GetBox().ExpandBy(Exit).Intersect(BP->Bounds.GetBox())))continue;
            if(!Existing&&ElectricalContacts.Num()>=8192){++ContactBudgetHits;continue;}
            auto& C=Existing?*Existing:ElectricalContacts.Add(Key);
            auto& R=C.Record;
            const bool SameShapes=C.PrimitiveA==AP&&C.PrimitiveB==BP;
            const bool WasValid=R.bValid;
            if(!SameShapes){C.Confirmations=0;R.bValid=false;}
            C.PrimitiveA=AP;C.PrimitiveB=BP;
            R.A=ContactBodyName(A);R.B=ContactBodyName(B);R.BodyA=A;R.BodyB=B;R.Channel=EReactiveContactChannel::Electrical;
            bool Touch=AC->bParticipatesInSimulation&&BC->bParticipatesInSimulation&&AP&&BP&&AP->IsRegistered()&&BP->IsRegistered()&&AP->IsCollisionEnabled()&&BP->IsCollisionEnabled()&&SharesReactionScope(AC,BC);
            FVector OnA=FVector::ZeroVector,OnB=FVector::ZeroVector;double Gap=-1;
            if(Touch&&AP->Bounds.GetBox().ExpandBy(Exit).Intersect(BP->Bounds.GetBox())&&AP->GetClosestPointOnCollision(BP->Bounds.Origin,OnA)>=0)
                Gap=BP->GetClosestPointOnCollision(OnA,OnB);
            Touch=Touch&&FMath::IsFinite(Gap)&&Gap>=0&&Gap<=(R.bValid?Exit:Enter)&&!OnA.ContainsNaN()&&!OnB.ContainsNaN();
            if(Touch)
            {
                FCollisionQueryParams Q(SCENE_QUERY_STAT(ReactiveElectricalContact),false);Q.AddIgnoredActor(AC->GetOwner());Q.AddIgnoredActor(BC->GetOwner());
                Touch=!GetWorld()->LineTraceTestByChannel(OnA,OnB,ECC_Visibility,Q);
            }
            if(Touch)
            {
                C.Confirmations=C.LastObservedStep+1==StepId?FMath::Min(Confirm,C.Confirmations+1):1;
                R.bValid=R.bValid||C.Confirmations>=Confirm;R.PositionCm=(OnA+OnB)*.5;
                if(R.bValid)R.LastConfirmedStep=StepId;
            }
            else{C.Confirmations=0;R.bValid=false;}
            C.LastObservedStep=StepId;
            if(WasValid!=R.bValid||!SameShapes||R.Version==0)R.Version=NextContactVersion++;
        }
    }
    for(auto& Pair:ElectricalContacts)if(Pair.Value.LastObservedStep!=StepId&&Pair.Value.Record.bValid)
    {Pair.Value.Record.bValid=false;Pair.Value.Confirmations=0;Pair.Value.Record.Version=NextContactVersion++;}
    for(const auto& Pair:ElectricalContacts)if(Pair.Value.Record.bValid)
    {const auto& R=Pair.Value.Record;ElectricalAdjacency.FindOrAdd(R.BodyA).Add(R.BodyB);ElectricalAdjacency.FindOrAdd(R.BodyB).Add(R.BodyA);}
    for(auto& Pair:ElectricalAdjacency)Pair.Value.Sort();
}
bool UReactiveWorldSubsystem::CanBodiesTransferLiquid(Reactive::FBodyId A,Reactive::FBodyId B) const
{
    auto* AC=Components.FindRef(A).Get();auto* BC=Components.FindRef(B).Get();
    if(!IsValid(AC)||!IsValid(BC)||!AC->bParticipatesInSimulation||!BC->bParticipatesInSimulation)return false;
    auto* AP=AC->GetPrimitive();auto* BP=BC->GetPrimitive();
    if(!AP||!BP||!AP->IsRegistered()||!BP->IsRegistered()||!AP->IsCollisionEnabled()||!BP->IsCollisionEnabled())return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ReactiveLiquidTransfer),false);Q.AddIgnoredActor(AC->GetOwner());Q.AddIgnoredActor(BC->GetOwner());
    return !GetWorld()->LineTraceTestByChannel(AP->Bounds.Origin,BP->Bounds.Origin,ECC_Visibility,Q);
}
void UReactiveWorldSubsystem::AdvanceLiquidPorts(double Step,uint64 StepId)
{
    TMap<FName,Reactive::FBodyId> Names;TSet<FName> Ambiguous;
    TArray<Reactive::FBodyId> IDs;Components.GetKeys(IDs);IDs.Sort();
    for(auto Id:IDs)if(auto* B=Components.FindRef(Id).Get();B&&!B->StableId.IsNone())
    {if(Names.Contains(B->StableId))Ambiguous.Add(B->StableId);else Names.Add(B->StableId,Id);}
    TSet<uint64> Seen;
    for(auto A:IDs)
    {
        auto* From=Components.FindRef(A).Get();if(!IsValid(From)||From->LiquidPorts.IsEmpty()||From->LiquidPorts.Num()>16||Ambiguous.Contains(From->StableId))continue;
        TSet<FName> PortIDs,Targets;
        for(const auto& Port:From->LiquidPorts)
        {
            if(!Port.bEnabled||Port.PortId.IsNone()||PortIDs.Contains(Port.PortId)||Targets.Contains(Port.TargetStableId))continue;
            PortIDs.Add(Port.PortId);Targets.Add(Port.TargetStableId);
            const auto B=Names.FindRef(Port.TargetStableId);auto* To=Components.FindRef(B).Get();
            if(!IsValid(To)||A==B||Ambiguous.Contains(Port.TargetStableId))continue;
            const uint64 Key=(uint64(A)<<32)|B;Seen.Add(Key);
            if(!LiquidContacts.Contains(Key)&&LiquidContacts.Num()>=4096){++ContactBudgetHits;continue;}
            auto& R=LiquidContacts.FindOrAdd(Key);const bool WasValid=R.bValid;R.bValid=false;
            R.A=ContactBodyName(A);R.B=ContactBodyName(B);R.BodyA=A;R.BodyB=B;R.Channel=EReactiveContactChannel::Liquid;
            const auto* AS=Simulation->Find(A);const auto* BS=Simulation->Find(B);
            const auto* AM=Simulation->FindMaterial(A);const auto* BM=Simulation->FindMaterial(B);
            auto* AP=From->GetPrimitive();auto* BP=To->GetPrimitive();
            const bool Parameters=FMath::IsFinite(Port.MaxGapCm)&&Port.MaxGapCm>=0&&Port.MaxGapCm<=100
                &&FMath::IsFinite(Port.MaxKgPerSecond)&&Port.MaxKgPerSecond>0&&Port.MaxKgPerSecond<=10
                &&!Port.LocalPositionCm.ContainsNaN()&&!Port.TargetLocalPositionCm.ContainsNaN();
            if(Parameters&&AS&&BS&&AM&&BM&&AM->bLiquidConductor&&BM->bLiquidConductor&&AM->WaterCapacityKg>0&&BM->WaterCapacityKg>0
                &&!AS->bBroken&&!BS->bBroken&&AS->IceFraction<.95&&BS->IceFraction<.95&&AP&&BP&&SharesReactionScope(From,To))
            {
                const FVector P=From->GetOwner()->GetActorTransform().TransformPosition(Port.LocalPositionCm);
                const FVector Q=To->GetOwner()->GetActorTransform().TransformPosition(Port.TargetLocalPositionCm);
                FCollisionQueryParams Trace(SCENE_QUERY_STAT(ReactiveLiquidPort),false);Trace.AddIgnoredActor(From->GetOwner());Trace.AddIgnoredActor(To->GetOwner());
                R.bValid=FVector::DistSquared(P,Q)<=FMath::Square(Port.MaxGapCm)&&AP->Bounds.GetBox().ExpandBy(2).IsInsideOrOn(P)&&BP->Bounds.GetBox().ExpandBy(2).IsInsideOrOn(Q)
                    &&CanBodiesTransferLiquid(A,B)&&!GetWorld()->LineTraceTestByChannel(P,Q,ECC_Visibility,Trace);
                R.PositionCm=(P+Q)*.5;
                if(R.bValid)
                {
                    R.LastConfirmedStep=StepId;
                    // Directed equalisation: only the fuller port drains; never overshoot equal fill fractions.
                    const double Equalise=(AS->WaterKg/AM->WaterCapacityKg-BS->WaterKg/BM->WaterCapacityKg)/(1/AM->WaterCapacityKg+1/BM->WaterCapacityKg);
                    if(Equalise>0)TransferWater(From,To,FMath::Min(Equalise,Port.MaxKgPerSecond*Step));
                }
            }
            if(WasValid!=R.bValid||R.Version==0)R.Version=NextContactVersion++;
        }
    }
    for(auto It=LiquidContacts.CreateIterator();It;++It)if(!Seen.Contains(It.Key()))It.RemoveCurrent();
}
void UReactiveWorldSubsystem::PublishElectricalWindows()
{
    for(const auto& Core:Simulation->DrainElectricalWindows())
    {
        auto* Body=Components.FindRef(Core.Receiver).Get();if(!IsValid(Body))continue;
        FReactiveElectricalWindow Window;Window.StepId=Core.StepId;Window.ReceiverId=Core.ReceiverId;
        Window.DeliveredJ=Core.DeliveredJ;Window.HeatJ=Core.HeatJ;Window.UsefulJ=Core.UsefulJ;Window.DurationSeconds=Core.DurationSeconds;
        for(const auto& E:Core.Contributions)
        {
            FReactiveElectricalExposure X;X.Source=GetBodyOwner(E.Source);X.Receiver=GetBodyOwner(E.Receiver);
            X.SourceStableId=ContactBodyName(E.Source);X.ReceiverStableId=ContactBodyName(E.Receiver);
            X.ReactionId=E.ReactionId;X.RootCauseId=E.RootCauseId;X.StepId=E.StepId;X.ReceiverId=E.ReceiverId;
            X.DeliveredJ=E.DeliveredJ;X.HeatJ=E.HeatJ;X.UsefulJ=E.UsefulJ;X.DurationSeconds=E.DurationSeconds;
            X.Contact.A=ContactBodyName(E.Contact.A);X.Contact.B=ContactBodyName(E.Contact.B);X.Contact.BodyA=E.Contact.A;X.Contact.BodyB=E.Contact.B;
            X.Contact.bValid=E.Contact.bValid;X.Contact.Version=E.Contact.Version;X.Contact.LastConfirmedStep=E.Contact.ConfirmedStep;X.Contact.PositionCm=E.Contact.PositionCm;
            Window.Contributions.Add(X);
        }
        if(auto* Previous=ElectricalRecipients.Find(Core.ReceiverId);Previous&&Previous->IsValid()&&Previous->Get()!=Body)Previous->Get()->ResetElectricalWindow();
        if(Core.DeliveredJ>0)ElectricalRecipients.Add(Core.ReceiverId,Body);else ElectricalRecipients.Remove(Core.ReceiverId);
        Body->AcceptElectricalWindow(Window);
    }
}
