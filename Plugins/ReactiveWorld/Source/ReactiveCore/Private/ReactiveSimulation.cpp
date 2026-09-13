#include "ReactiveSimulation.h"

namespace Reactive
{
namespace
{
bool Finite(double V) { return FMath::IsFinite(V); }
bool PositionValid(const FVector& V) { return !V.ContainsNaN() && V.GetAbsMax() <= 1.e8; }
double WaterEnthalpy(double T) { return T < 0.0 ? IceCp * T : FusionJPerKg + WaterCp * T; }
double SafeCapacity(const FMaterial& M, double Water) { return M.DryCapacity() + Water * IceCp; }
}

FMaterial FMaterial::Wood() { return FMaterial(); }
FMaterial FMaterial::Metal()
{
    FMaterial M; M.DryMassKg = 0.2; M.SpecificHeatJPerKgK = 500; M.InitialFuelKg = 0;
    M.Conductivity = 1; M.ThermalCouplingWPerK = 12; M.StrengthNs = 80; return M;
}
FMaterial FMaterial::Water()
{
    FMaterial M; M.DryMassKg = 0.001; M.SpecificHeatJPerKgK = 1000; M.WaterCapacityKg = 1;
    M.InitialFuelKg = 0; M.Conductivity = 0; M.ThermalCouplingWPerK = 8; M.StrengthNs = 10; return M;
}
FMaterial FMaterial::Oil()
{
    FMaterial M; M.IgnitionC = 220; M.InitialFuelKg = 0.1; M.BurnRateKgPerSec = 0.004;
    M.CombustionJPerKg = 42000000; M.WaterCapacityKg = 0.005; return M;
}
FMaterial FMaterial::Stone()
{
    FMaterial M; M.DryMassKg = 0.5; M.SpecificHeatJPerKgK = 800; M.InitialFuelKg = 0;
    M.Conductivity = 0; M.StrengthNs = 150; M.ThermalCouplingWPerK = 1; return M;
}
bool FMaterial::IsValid() const
{
    const double Values[] = { DryMassKg, SpecificHeatJPerKgK, WaterCapacityKg, InitialFuelKg,
        IgnitionC, BurnRateKgPerSec, CombustionJPerKg, RetainedHeatFraction, Conductivity,
        ThermalCouplingWPerK, CoolingWPerK, StrengthNs, FrozenStrengthMultiplier,
        SealedVolumeM3, BurstGaugePressurePa };
    for (double V : Values) { if (!Finite(V)) return false; }
    return DryMassKg > 0 && SpecificHeatJPerKgK > 0 && WaterCapacityKg >= 0 && InitialFuelKg >= 0
        && IgnitionC > 100 && BurnRateKgPerSec >= 0 && CombustionJPerKg >= 0
        && RetainedHeatFraction >= 0 && RetainedHeatFraction <= 1 && Conductivity >= 0 && Conductivity <= 1
        && ThermalCouplingWPerK >= 0 && CoolingWPerK >= 0 && StrengthNs > 0
        && FrozenStrengthMultiplier > 0 && FrozenStrengthMultiplier <= 1
        && SealedVolumeM3 >= 0 && BurstGaugePressurePa > 0;
}

FSimulation::FSimulation(const FSettings& InSettings) : Settings(InSettings)
{
    Settings.StepSeconds = Finite(Settings.StepSeconds) ? FMath::Clamp(Settings.StepSeconds, 0.001, 0.1) : 0.05;
    Settings.CellSizeCm = Finite(Settings.CellSizeCm) ? FMath::Clamp(Settings.CellSizeCm, 50.0, 1000.0) : 200.0;
    Settings.HeatReachCm = Finite(Settings.HeatReachCm) ? FMath::Clamp(Settings.HeatReachCm, 0.0, 1000.0) : 250.0;
    Settings.MaxBodies = FMath::Clamp(Settings.MaxBodies, 1, 100000);
    Settings.MaxPendingInputs = FMath::Clamp(Settings.MaxPendingInputs, 1, 4096);
    Settings.MaxThermalPairs = FMath::Max(1, Settings.MaxThermalPairs);
    Settings.MaxElectricalNodes = FMath::Max(1, Settings.MaxElectricalNodes);
    Settings.MaxElectricalHops = FMath::Clamp(Settings.MaxElectricalHops, 0, 32);
}
double FSimulation::InitialEnthalpy(const FMaterial& M, double T, double Water)
{
    return M.DryCapacity() * T + Water * WaterEnthalpy(T);
}
FIntVector FSimulation::CellFor(const FVector& P) const
{
    return FIntVector(FMath::FloorToInt(P.X / Settings.CellSizeCm), FMath::FloorToInt(P.Y / Settings.CellSizeCm),
        FMath::FloorToInt(P.Z / Settings.CellSizeCm));
}
FBodyId FSimulation::Register(const FMaterial& M, const FVector& P, double R, double T, double Water)
{
    if (!M.IsValid() || !PositionValid(P) || !Finite(R) || R <= 0 || R > 1000 || !Finite(T)
        || T < -200 || T > 100 || !Finite(Water) || Water < 0 || Water > M.WaterCapacityKg
        || Bodies.Num() >= Settings.MaxBodies || NextId == InvalidBody)
    { ++Stats.RejectedInputs; return InvalidBody; }
    const FBodyId Id = NextId++;
    FBody B; B.Material = M; B.Position = P; B.RadiusCm = R; B.Cell = CellFor(P);
    B.State.WaterKg = Water; B.State.FuelKg = M.InitialFuelKg; B.State.TemperatureC = T;
    B.State.EnthalpyJ = InitialEnthalpy(M, T, Water); B.State.IceFraction = T < 0 && Water > 0 ? 1 : 0;
    Grid.FindOrAdd(B.Cell).Add(Id); Bodies.Add(Id, B); MaxRadiusCm = FMath::Max(MaxRadiusCm, R);
    Wake(Id); Stats.Registered = Bodies.Num(); return Id;
}
void FSimulation::Unregister(FBodyId Id)
{
    if (const FBody* B = Bodies.Find(Id))
    {
        const FIntVector Cell = B->Cell;
        if (TArray<FBodyId>* Bucket = Grid.Find(Cell)) { Bucket->Remove(Id); if (Bucket->IsEmpty()) Grid.Remove(Cell); }
        Bodies.Remove(Id); Active.Remove(Id); Stats.Registered = Bodies.Num();
    }
}
bool FSimulation::Move(FBodyId Id, const FVector& P)
{
    FBody* B = Bodies.Find(Id);
    if (!B || !PositionValid(P)) return false;
    if (B->Position.Equals(P, 0.01)) return true;
    const FIntVector NewCell = CellFor(P);
    if (NewCell != B->Cell)
    {
        if (TArray<FBodyId>* Bucket = Grid.Find(B->Cell))
        { Bucket->Remove(Id); if (Bucket->IsEmpty()) Grid.Remove(B->Cell); }
        Grid.FindOrAdd(NewCell).Add(Id); B->Cell = NewCell;
    }
    B->Position = P; Wake(Id); return true;
}
void FSimulation::Wake(FBodyId Id)
{
    if (FBody* B = Bodies.Find(Id)) { B->QuietSeconds = 0; Active.Add(Id); }
}
bool FSimulation::Enqueue(const FStimulus& S)
{
    if (!PositionValid(S.PositionCm) || !Finite(S.RadiusCm) || S.RadiusCm < 0 || S.RadiusCm > 2000
        || !Finite(S.HeatJ) || FMath::Abs(S.HeatJ) > 1.e8 || !Finite(S.WaterKg) || S.WaterKg < 0 || S.WaterKg > 100
        || !Finite(S.ElectricalJ) || S.ElectricalJ < 0 || S.ElectricalJ > 1.e8
        || S.ImpulseNs.ContainsNaN() || S.ImpulseNs.Size() > 1.e6
        || (S.Target != InvalidBody && !Bodies.Contains(S.Target)) || Pending.Num() >= Settings.MaxPendingInputs)
    { ++Stats.RejectedInputs; return false; }
    Pending.Add(S); return true;
}
bool FSimulation::SetEnvironment(const FEnvironment& E)
{
    if (!Finite(E.TemperatureC) || E.TemperatureC < -100 || E.TemperatureC > 100
        || !Finite(E.RainKgPerM2Sec) || E.RainKgPerM2Sec < 0 || E.RainKgPerM2Sec > 1
        || E.WindMPerSec.ContainsNaN() || E.WindMPerSec.Size() > 100)
    { ++Stats.RejectedInputs; return false; }
    Environment = E;
    for (const auto& Pair : Bodies) Wake(Pair.Key);
    return true;
}
const FState* FSimulation::Find(FBodyId Id) const { const FBody* B = Bodies.Find(Id); return B ? &B->State : nullptr; }
const FMaterial* FSimulation::FindMaterial(FBodyId Id) const { const FBody* B = Bodies.Find(Id); return B ? &B->Material : nullptr; }
TArray<FEvent> FSimulation::DrainEvents() { TArray<FEvent> Result = MoveTemp(Events); Events.Reset(); return Result; }
void FSimulation::Emit(FBodyId Id, EEvent Kind, double Magnitude, const FVector& Vector)
{
    // Draining once per fixed step is part of the host contract. Bound abandoned consumers.
    if (Events.Num() >= 65536) { ++Stats.BudgetHits; return; }
    const FBody* B = Bodies.Find(Id);
    Events.Add({ NextEvent++, Id, Kind, Magnitude, Vector, B ? B->State.LastSource : InvalidBody });
}
TArray<FBodyId> FSimulation::Query(const FVector& P, double R) const
{
    TArray<FBodyId> Result;
    if (!PositionValid(P) || !Finite(R) || R < 0 || R > 5000) return Result;
    const FVector Extent(R + MaxRadiusCm);
    const FIntVector Min = CellFor(P - Extent), Max = CellFor(P + Extent);
    for (int32 X = Min.X; X <= Max.X; ++X)
    for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
    for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
    {
        if (const TArray<FBodyId>* Bucket = Grid.Find(FIntVector(X, Y, Z)))
        for (FBodyId Id : *Bucket)
        {
            const FBody& B = Bodies.FindChecked(Id);
            if (FVector::DistSquared(P, B.Position) <= FMath::Square(R + B.RadiusCm)) Result.Add(Id);
        }
    }
    Result.Sort(); return Result;
}

void FSimulation::Resolve(FBodyId Id, FBody& B)
{
    FState& S = B.State;
    const double PreviousIce = S.IceFraction;
    const double C = B.Material.DryCapacity();
    // Magic extraction has a gameplay floor; excess extraction is rejected by this clamp.
    S.EnthalpyJ = FMath::Max(S.EnthalpyJ, -200.0 * (C + S.WaterKg * IceCp));
    const double BoilH = C * 100 + S.WaterKg * (FusionJPerKg + WaterCp * 100);
    if (S.WaterKg > 0 && S.EnthalpyJ > BoilH)
    {
        const double VaporKg = FMath::Min(S.WaterKg, (S.EnthalpyJ - BoilH) / VaporizationJPerKg);
        const double CarriedJ = VaporKg * (FusionJPerKg + WaterCp * 100 + VaporizationJPerKg);
        S.WaterKg -= VaporKg; S.EnthalpyJ -= CarriedJ;
        if (B.Material.SealedVolumeM3 > 0 && !S.bBurst) S.GasEnergyJ += CarriedJ;
        else Stats.VentedEnergyJ += CarriedJ;
        if (VaporKg > 1.e-9) Emit(Id, EEvent::Steam, VaporKg);
    }
    if (S.WaterKg < 1.e-10) { S.WaterKg = 0; S.IceFraction = 0; S.TemperatureC = S.EnthalpyJ / C; }
    else if (S.EnthalpyJ < 0)
    { S.TemperatureC = S.EnthalpyJ / (C + S.WaterKg * IceCp); S.IceFraction = 1; }
    else if (S.EnthalpyJ <= S.WaterKg * FusionJPerKg)
    { S.TemperatureC = 0; S.IceFraction = 1 - S.EnthalpyJ / (S.WaterKg * FusionJPerKg); }
    else
    { S.TemperatureC = (S.EnthalpyJ - S.WaterKg * FusionJPerKg) / (C + S.WaterKg * WaterCp); S.IceFraction = 0; }
    if (PreviousIce < 0.95 && S.IceFraction >= 0.95) Emit(Id, EEvent::Frozen, S.WaterKg);
    if (PreviousIce >= 0.95 && S.IceFraction < 0.95) Emit(Id, EEvent::Thawed, S.WaterKg);
}
void FSimulation::Apply(FBodyId Id, const FStimulus& Input, double Weight)
{
    FBody* B = Bodies.Find(Id); if (!B) return;
    Wake(Id);
    FState& S = B->State;
    if (FMath::Abs(Input.HeatJ) > 1 || Input.ElectricalJ > 0 || !Input.ImpulseNs.IsNearlyZero()) S.LastSource = Input.Source;
    const double Water = FMath::Min(Input.WaterKg * Weight, FMath::Max(0.0, B->Material.WaterCapacityKg - S.WaterKg));
    Stats.RejectedWaterKg += Input.WaterKg * Weight - Water;
    S.WaterKg += Water;
    S.EnthalpyJ += Input.HeatJ * Weight + Water * WaterEnthalpy(FMath::Max(0.0, Environment.TemperatureC));
    Resolve(Id, *B);
    const FVector Impulse = Input.ImpulseNs * Weight;
    if (!Impulse.IsNearlyZero())
    {
        const double Strength = B->Material.StrengthNs * FMath::Lerp(1.0, B->Material.FrozenStrengthMultiplier, S.IceFraction);
        S.Integrity = FMath::Max(0.0, S.Integrity - Impulse.Size() / Strength);
        Emit(Id, EEvent::Impulse, 0, Impulse);
    }
    if (Input.ElectricalJ > 0) Conduct(Id, Input.ElectricalJ * Weight, Input.Source);
}
double FSimulation::Conductivity(const FBody& B) const
{
    const double Liquid = B.State.WaterKg * (1 - B.State.IceFraction);
    const double Wet = B.Material.WaterCapacityKg > 0 ? Liquid / B.Material.WaterCapacityKg : 0;
    return FMath::Clamp(B.Material.Conductivity + Wet * 0.8, 0.0, 1.0);
}
void FSimulation::Conduct(FBodyId Origin, double EnergyJ, FBodyId Source)
{
    struct FPacket { FBodyId Id; double Energy; int32 Depth; };
    TArray<FPacket> Queue; Queue.Add({ Origin, EnergyJ, 0 });
    TSet<FBodyId> Visited; Visited.Add(Origin);
    Stats.ElectricalInputJ += EnergyJ;
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const FPacket Packet = Queue[Index]; FBody& B = Bodies.FindChecked(Packet.Id);
        ++Stats.ElectricalVisits;
        TArray<FBodyId> Next; double WeightSum = 0;
        if (Packet.Depth < Settings.MaxElectricalHops && Conductivity(B) > 0.05)
        for (FBodyId Candidate : Query(B.Position, B.RadiusCm + 1.0))
        {
            if (Visited.Contains(Candidate)) continue;
            const FBody& N = Bodies.FindChecked(Candidate);
            if (Conductivity(N) <= 0.05 || (CanExchange && !CanExchange(Packet.Id, Candidate))) continue;
            if (Visited.Num() >= Settings.MaxElectricalNodes) { ++Stats.BudgetHits; break; }
            Next.Add(Candidate); Visited.Add(Candidate); WeightSum += Conductivity(N);
        }
        const double Deposited = Next.IsEmpty() ? Packet.Energy : Packet.Energy * 0.35;
        B.State.LastSource = Source;
        B.State.EnthalpyJ += Deposited; Stats.ElectricalDepositedJ += Deposited;
        Wake(Packet.Id); Resolve(Packet.Id, B); Emit(Packet.Id, EEvent::Shock, Deposited);
        for (FBodyId Candidate : Next)
            Queue.Add({ Candidate, (Packet.Energy - Deposited) * Conductivity(Bodies.FindChecked(Candidate)) / WeightSum, Packet.Depth + 1 });
    }
}

void FSimulation::ExchangeHeat()
{
    struct FPair { FBodyId Hot; FBodyId Cold; double Joules; };
    TArray<FPair> Pairs; TSet<uint64> Seen;
    TMap<FBodyId, double> Outgoing, Incoming, MinT, MaxT;
    TArray<FBodyId> Sources = Active.Array(); Sources.Sort();
    for (FBodyId Id : Sources)
    {
        const FBody& A = Bodies.FindChecked(Id);
        for (FBodyId Other : Query(A.Position, A.RadiusCm + Settings.HeatReachCm))
        {
            if (Id == Other) continue;
            const uint64 Key = (uint64(FMath::Min(Id, Other)) << 32) | FMath::Max(Id, Other);
            if (Seen.Contains(Key)) continue;
            Seen.Add(Key);
            const FBody& B = Bodies.FindChecked(Other);
            const double Diff = A.State.TemperatureC - B.State.TemperatureC;
            if (FMath::Abs(Diff) < 0.01 || (CanExchange && !CanExchange(Id, Other))) continue;
            if (Pairs.Num() >= Settings.MaxThermalPairs) { ++Stats.BudgetHits; break; }
            const double Gap = FMath::Max(0.0, FVector::Distance(A.Position, B.Position) - A.RadiusCm - B.RadiusCm);
            const double Falloff = 1 - FMath::Clamp(Gap / FMath::Max(1.0, Settings.HeatReachCm), 0.0, 1.0);
            const FBodyId Hot = Diff > 0 ? Id : Other, Cold = Diff > 0 ? Other : Id;
            const FBody& H = Bodies.FindChecked(Hot); const FBody& C = Bodies.FindChecked(Cold);
            const double Wind = 1 + FMath::Clamp(FVector::DotProduct((C.Position - H.Position).GetSafeNormal(), Environment.WindMPerSec) * 0.05, -0.5, 1.0);
            const double J = FMath::Min(A.Material.ThermalCouplingWPerK, B.Material.ThermalCouplingWPerK)
                * FMath::Abs(Diff) * Falloff * Wind * Settings.StepSeconds;
            if (J < 1.e-8) continue;
            Pairs.Add({ Hot, Cold, J }); Outgoing.FindOrAdd(Hot) += J; Incoming.FindOrAdd(Cold) += J;
            double& Lo = MinT.FindOrAdd(Hot, H.State.TemperatureC); Lo = FMath::Min(Lo, C.State.TemperatureC);
            double& Hi = MaxT.FindOrAdd(Cold, C.State.TemperatureC); Hi = FMath::Max(Hi, H.State.TemperatureC);
        }
        if (Pairs.Num() >= Settings.MaxThermalPairs) break;
    }
    TMap<FBodyId, double> Deltas;
    TMap<FBodyId, double> StrongestHeat;
    TMap<FBodyId, FBodyId> HeatSources;
    for (const FPair& P : Pairs)
    {
        const FBody& H = Bodies.FindChecked(P.Hot); const FBody& C = Bodies.FindChecked(P.Cold);
        const double OutCap = 0.25 * SafeCapacity(H.Material, H.State.WaterKg) * (H.State.TemperatureC - MinT[P.Hot]);
        const double InCap = 0.25 * SafeCapacity(C.Material, C.State.WaterKg) * (MaxT[P.Cold] - C.State.TemperatureC);
        const double Scale = FMath::Min3(1.0, OutCap / Outgoing[P.Hot], InCap / Incoming[P.Cold]);
        Deltas.FindOrAdd(P.Hot) -= P.Joules * Scale; Deltas.FindOrAdd(P.Cold) += P.Joules * Scale;
        if (P.Joules * Scale > StrongestHeat.FindRef(P.Cold))
        { StrongestHeat.Add(P.Cold, P.Joules * Scale); HeatSources.Add(P.Cold, H.State.LastSource); }
    }
    TArray<FBodyId> Affected; Deltas.GetKeys(Affected); Affected.Sort();
    for (FBodyId Id : Affected)
    {
        FBody& B = Bodies.FindChecked(Id); B.State.EnthalpyJ += Deltas[Id];
        if (Deltas[Id] > 1 && HeatSources.Contains(Id)) B.State.LastSource = HeatSources[Id];
        if (FMath::Abs(Deltas[Id]) > 0.01) Wake(Id);
        Resolve(Id, B);
    }
    Stats.ThermalPairs = Pairs.Num();
}
void FSimulation::React(FBodyId Id, FBody& B)
{
    FState& S = B.State; const FMaterial& M = B.Material;
    const double Dt = Settings.StepSeconds;
    const double AreaM2 = PI * FMath::Square(B.RadiusCm / 100.0);
    const double Rain = FMath::Min(Environment.RainKgPerM2Sec * AreaM2 * Dt, FMath::Max(0.0, M.WaterCapacityKg - S.WaterKg));
    S.WaterKg += Rain; S.EnthalpyJ += Rain * WaterEnthalpy(FMath::Max(0.0, Environment.TemperatureC));
    const double TargetH = InitialEnthalpy(M, Environment.TemperatureC, S.WaterKg);
    const double Q = (Environment.TemperatureC - S.TemperatureC) * (M.CoolingWPerK + Environment.RainKgPerM2Sec * AreaM2 * 1000) * Dt;
    S.EnthalpyJ += FMath::Clamp(Q, -FMath::Abs(TargetH - S.EnthalpyJ) * 0.25, FMath::Abs(TargetH - S.EnthalpyJ) * 0.25);
    Resolve(Id, B);
    const double Wet = M.WaterCapacityKg > 0 ? S.WaterKg / M.WaterCapacityKg : 0;
    const bool ShouldBurn = S.FuelKg > 1.e-8 && Wet < 0.35 && S.TemperatureC >= M.IgnitionC - (S.bBurning ? 40 : 0);
    if (ShouldBurn != S.bBurning) { S.bBurning = ShouldBurn; Emit(Id, ShouldBurn ? EEvent::Ignited : EEvent::Extinguished); }
    if (S.bBurning)
    {
        const double BurnKg = FMath::Min(S.FuelKg, M.BurnRateKgPerSec * Dt);
        S.FuelKg -= BurnKg;
        const double Release = BurnKg * M.CombustionJPerKg;
        S.EnthalpyJ += Release * M.RetainedHeatFraction;
        if (M.SealedVolumeM3 > 0 && !S.bBurst) S.GasEnergyJ += Release * (1 - M.RetainedHeatFraction);
        else Stats.VentedEnergyJ += Release * (1 - M.RetainedHeatFraction);
        S.Integrity = FMath::Max(0.0, S.Integrity - BurnKg / FMath::Max(M.InitialFuelKg, 1.e-8));
        Resolve(Id, B);
    }
    S.GaugePressurePa = M.SealedVolumeM3 > 0 && !S.bBurst ? 0.4 * S.GasEnergyJ / M.SealedVolumeM3 : 0;
    if (M.SealedVolumeM3 > 0 && !S.bBurst && S.GaugePressurePa >= M.BurstGaugePressurePa)
    {
        S.bBurst = true; S.Integrity = 0;
        const double Energy = S.GasEnergyJ; S.GasEnergyJ = 0; S.GaugePressurePa = 0;
        Emit(Id, EEvent::Burst, Energy);
        const TArray<FBodyId> Nearby = Query(B.Position, 400);
        TArray<FBodyId> Targets;
        for (FBodyId Other : Nearby) if (Other != Id && (!CanExchange || CanExchange(Id, Other))) Targets.Add(Other);
        for (FBodyId Other : Targets)
        {
            const FBody& N = Bodies.FindChecked(Other); const double Share = Energy / Targets.Num();
            FStimulus Blast; Blast.Source = S.LastSource; Blast.Target = Other; Blast.HeatJ = Share * 0.2;
            Blast.ImpulseNs = (N.Position - B.Position).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector)
                * FMath::Sqrt(2 * N.Material.DryMassKg * Share * 0.8);
            if (!Enqueue(Blast)) Stats.VentedEnergyJ += Share;
        }
        if (Targets.IsEmpty()) Stats.VentedEnergyJ += Energy;
    }
    if (!S.bBroken && S.Integrity <= 0.05) { S.bBroken = true; Emit(Id, EEvent::Broken); }
    const bool NearAmbient = FMath::Abs(S.TemperatureC - Environment.TemperatureC) < 0.05;
    const bool RainStable = Environment.RainKgPerM2Sec <= 0 || S.WaterKg >= M.WaterCapacityKg - 1.e-8;
    if (!S.bBurning && NearAmbient && RainStable) B.QuietSeconds += Dt; else B.QuietSeconds = 0;
    if (B.QuietSeconds >= 1) Active.Remove(Id);
}
double FSimulation::TransferLiquid(FBodyId From, FBodyId To, double MaxKg)
{
    FBody* A = Bodies.Find(From); FBody* B = Bodies.Find(To);
    if (!A || !B || From == To || !Finite(MaxKg) || MaxKg <= 0 || (CanExchange && !CanExchange(From, To))) return 0;
    const double Kg = FMath::Min3(MaxKg, A->State.WaterKg * (1 - A->State.IceFraction),
        FMath::Max(0.0, B->Material.WaterCapacityKg - B->State.WaterKg));
    if (Kg <= 1.e-9) return 0;
    const double J = Kg * WaterEnthalpy(FMath::Max(0.0, A->State.TemperatureC));
    A->State.WaterKg -= Kg; A->State.EnthalpyJ -= J;
    B->State.WaterKg += Kg; B->State.EnthalpyJ += J; B->State.LastSource = A->State.LastSource;
    Resolve(From, *A); Resolve(To, *B); Wake(From); Wake(To);
    return Kg;
}
bool FSimulation::RestoreStates(const TMap<FBodyId, FState>& States)
{
    // Work on a copy to keep malformed saves from leaving a half-restored world.
    FSimulation Candidate = *this;
    Candidate.Events.Reset(); Candidate.Pending.Reset();
    for (const auto& Pair : States)
    {
        FBody* B = Candidate.Bodies.Find(Pair.Key); const FState& S = Pair.Value;
        if (!B || !Finite(S.EnthalpyJ) || !Finite(S.WaterKg) || !Finite(S.FuelKg) || !Finite(S.Integrity)
            || !Finite(S.GasEnergyJ) || FMath::Abs(S.EnthalpyJ) > 1.e12 || S.WaterKg < 0
            || S.WaterKg > B->Material.WaterCapacityKg || S.FuelKg < 0 || S.FuelKg > B->Material.InitialFuelKg
            || S.Integrity < 0 || S.Integrity > 1 || S.GasEnergyJ < 0 || S.GasEnergyJ > 1.e12
            || (S.bBurst && S.GasEnergyJ > 0) || (B->Material.SealedVolumeM3 == 0 && S.GasEnergyJ > 0)
            || (S.bBroken != (S.Integrity <= 0.05))) return false;
        B->State = S; B->State.LastSource = InvalidBody;
        Candidate.Resolve(Pair.Key, *B);
        if (FMath::Abs(B->State.WaterKg - S.WaterKg) > 1.e-8 || FMath::Abs(B->State.EnthalpyJ - S.EnthalpyJ) > 1.e-5) return false;
        B->State.GaugePressurePa = B->Material.SealedVolumeM3 > 0 && !S.bBurst ? 0.4 * S.GasEnergyJ / B->Material.SealedVolumeM3 : 0;
        Candidate.Wake(Pair.Key);
    }
    Candidate.Events.Reset(); Candidate.Changed.Reset();
    *this = MoveTemp(Candidate);
    return true;
}
double FSimulation::WithdrawLiquid(FBodyId From, double MaxKg)
{
    FBody* B = Bodies.Find(From);
    if (!B || !Finite(MaxKg) || MaxKg <= 0) return 0;
    const double Kg = FMath::Min(MaxKg, B->State.WaterKg * (1 - B->State.IceFraction));
    const double J = Kg * WaterEnthalpy(FMath::Max(0.0, B->State.TemperatureC));
    B->State.WaterKg -= Kg; B->State.EnthalpyJ -= J; Resolve(From, *B); Wake(From); return Kg;
}
void FSimulation::Step()
{
    Changed.Reset(); Stats.ElectricalVisits = 0;
    TArray<FStimulus> Inputs = MoveTemp(Pending); Pending.Reset();
    for (const FStimulus& Input : Inputs)
    {
        if (Input.Target != InvalidBody) Apply(Input.Target, Input, 1);
        else
        {
            const TArray<FBodyId> Targets = Query(Input.PositionCm, Input.RadiusCm);
            for (FBodyId Id : Targets) Apply(Id, Input, 1.0 / Targets.Num());
        }
    }
    ExchangeHeat();
    TArray<FBodyId> Work = Active.Array(); Work.Sort();
    for (FBodyId Id : Work) { React(Id, Bodies.FindChecked(Id)); Changed.Add(Id); }
    Stats.Active = Active.Num(); ++Stats.Steps;
}
}
