#include "ReactiveSimulation.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
using namespace Reactive;
namespace
{
FMaterial Isolated(FMaterial M) { M.CoolingWPerK = 0; M.ThermalCouplingWPerK = 0; return M; }
void Steps(FSimulation& Sim, int32 Count) { for (int32 I = 0; I < Count; ++I) Sim.Step(); }
void Heat(FSimulation& Sim, FBodyId Id, double J) { FStimulus S; S.Target = Id; S.HeatJ = J; Sim.Enqueue(S); Sim.Step(); }
int32 CountEvent(const TArray<Reactive::FEvent>& Events, EEvent Kind)
{ int32 N = 0; for (const Reactive::FEvent& E : Events) if (E.Kind == Kind) ++N; return N; }
constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveBurnTest, "Reactive.Core.BurnExtinguish", TestFlags)
bool FReactiveBurnTest::RunTest(const FString&)
{
    FSimulation Sim; FMaterial Wood = Isolated(FMaterial::Wood()); Wood.WaterCapacityKg = 1;
    const auto Id = Sim.Register(Wood, FVector::ZeroVector, 50);
    Heat(Sim, Id, 60000); TestTrue(TEXT("Enough heat ignites fuel"), Sim.Find(Id)->bBurning);
    FStimulus Water; Water.Target = Id; Water.WaterKg = 1; Sim.Enqueue(Water); Sim.Step();
    TestFalse(TEXT("Water cools and extinguishes"), Sim.Find(Id)->bBurning);
    TestTrue(TEXT("Mixing temperature falls"), Sim.Find(Id)->TemperatureC < 100);
    const auto Events = Sim.DrainEvents();
    TestEqual(TEXT("Ignited once"), CountEvent(Events, EEvent::Ignited), 1);
    TestEqual(TEXT("Extinguished once"), CountEvent(Events, EEvent::Extinguished), 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveLatentTest, "Reactive.Core.LatentHeat", TestFlags)
bool FReactiveLatentTest::RunTest(const FString&)
{
    FSimulation Sim; const auto Id = Sim.Register(Isolated(FMaterial::Water()), FVector::ZeroVector, 50, 20, 1);
    Heat(Sim, Id, -90000);
    TestEqual(TEXT("Phase plateau at zero C"), Sim.Find(Id)->TemperatureC, 0.0);
    TestTrue(TEXT("Small extraction does not instantly freeze all water"), Sim.Find(Id)->IceFraction > 0 && Sim.Find(Id)->IceFraction < 0.1);
    Heat(Sim, Id, -350000); TestTrue(TEXT("Fusion energy is required for full freezing"), Sim.Find(Id)->IceFraction > 0.99);
    Heat(Sim, Id, 500000); TestTrue(TEXT("Adding energy melts ice"), Sim.Find(Id)->IceFraction < 0.01);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveSteamTest, "Reactive.Core.SteamEnergyMassBalance", TestFlags)
bool FReactiveSteamTest::RunTest(const FString&)
{
    FSimulation Sim; const auto Id = Sim.Register(Isolated(FMaterial::Water()), FVector::ZeroVector, 50, 100, 1);
    const double Before = Sim.Find(Id)->EnthalpyJ;
    Heat(Sim, Id, VaporizationJPerKg * 0.1);
    TestTrue(TEXT("Exactly 0.1 kg vaporized"), FMath::IsNearlyEqual(Sim.Find(Id)->WaterKg, 0.9, 1.e-8));
    TestTrue(TEXT("Remaining liquid stays at boiling point"), FMath::IsNearlyEqual(Sim.Find(Id)->TemperatureC, 100.0, 1.e-8));
    TestTrue(TEXT("Vapor carries sensible and latent energy out"), FMath::IsNearlyEqual(Before + VaporizationJPerKg * 0.1,
        Sim.Find(Id)->EnthalpyJ + Sim.GetStats().VentedEnergyJ, 1.e-6));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveElectricalTest, "Reactive.Core.ElectricalCycleConservation", TestFlags)
bool FReactiveElectricalTest::RunTest(const FString&)
{
    FSimulation Sim; const auto M = Isolated(FMaterial::Metal());
    const auto A = Sim.Register(M, FVector(0,0,0), 50);
    Sim.Register(M, FVector(90,0,0), 50); Sim.Register(M, FVector(45,78,0), 50);
    FStimulus S; S.Target = A; S.ElectricalJ = 1000; Sim.Enqueue(S); Sim.Step();
    const auto Events = Sim.DrainEvents();
    TestEqual(TEXT("Each cyclic node visited once"), CountEvent(Events, EEvent::Shock), 3);
    TestTrue(TEXT("Branches do not duplicate joules"), FMath::IsNearlyEqual(Sim.GetStats().ElectricalDepositedJ + Sim.GetStats().ElectricalLostJ, 1000.0, 1.e-7));
    double Sum = 0; for (const auto& E : Events) if (E.Kind == EEvent::Shock) Sum += E.Magnitude;
    TestTrue(TEXT("Shock dose totals the delivered energy"), FMath::IsNearlyEqual(Sum, Sim.GetStats().ElectricalDepositedJ, 1.e-7));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveWaterConductionTest, "Reactive.Core.WaterAndMetalConduction", TestFlags)
bool FReactiveWaterConductionTest::RunTest(const FString&)
{
    FSimulation Sim;
    const auto Water = Sim.Register(Isolated(FMaterial::Water()), FVector::ZeroVector, 100, 20, 1);
    Sim.Register(Isolated(FMaterial::Metal()), FVector(140,0,0), 50);
    Sim.Register(Isolated(FMaterial::Metal()), FVector(-140,0,0), 50);
    Sim.Register(Isolated(FMaterial::Metal()), FVector(0,140,0), 50); // Player uses exactly the same path.
    FStimulus S; S.Target = Water; S.ElectricalJ = 100; Sim.Enqueue(S); Sim.Step();
    TestEqual(TEXT("Puddle reaches enemies and player without team exemption"), CountEvent(Sim.DrainEvents(), EEvent::Shock), 4);
    FSimulation Dry;
    const auto D = Dry.Register(Isolated(FMaterial::Water()), FVector::ZeroVector, 100, 20, 0);
    Dry.Register(Isolated(FMaterial::Metal()), FVector(140,0,0), 50);
    S.Target = D; Dry.Enqueue(S); Dry.Step();
    TestEqual(TEXT("Dry authored water has no receiver load or conductive path"), CountEvent(Dry.DrainEvents(), EEvent::Shock), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveOcclusionTest, "Reactive.Core.ContactAndOcclusion", TestFlags)
bool FReactiveOcclusionTest::RunTest(const FString&)
{
    FSimulation Sim; const auto M = Isolated(FMaterial::Metal());
    const auto A = Sim.Register(M, FVector::ZeroVector, 50);
    Sim.Register(M, FVector(90,0,0), 50); Sim.Register(M, FVector(500,0,0), 50);
    Sim.CanExchange = [](FBodyId, FBodyId) { return false; };
    FStimulus S; S.Target = A; S.ElectricalJ = 1000; Sim.Enqueue(S); Sim.Step();
    TestEqual(TEXT("Occlusion blocks electrical path"), CountEvent(Sim.DrainEvents(), EEvent::Shock), 1);
    Sim.CanExchange = nullptr; Sim.Enqueue(S); Sim.Step();
    TestEqual(TEXT("No arc across an air gap"), CountEvent(Sim.DrainEvents(), EEvent::Shock), 2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveThermalTest, "Reactive.Core.ThermalConservationAndStability", TestFlags)
bool FReactiveThermalTest::RunTest(const FString&)
{
    FSimulation Sim; auto M = Isolated(FMaterial::Metal()); M.ThermalCouplingWPerK = 1.e8;
    const auto A = Sim.Register(M, FVector::ZeroVector, 50, 100);
    const auto B = Sim.Register(M, FVector(100,0,0), 50, 20);
    const double Total = Sim.Find(A)->EnthalpyJ + Sim.Find(B)->EnthalpyJ;
    Steps(Sim, 100);
    TestTrue(TEXT("Pair transfer conserves joules"), FMath::IsNearlyEqual(Total, Sim.Find(A)->EnthalpyJ + Sim.Find(B)->EnthalpyJ, 1.e-6));
    TestTrue(TEXT("Huge coupling cannot overshoot or oscillate"), Sim.Find(A)->TemperatureC >= 20 && Sim.Find(B)->TemperatureC <= 100);
    TestTrue(TEXT("Converges toward equal temperature"), FMath::Abs(Sim.Find(A)->TemperatureC - Sim.Find(B)->TemperatureC) < 0.1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveSpatialTest, "Reactive.Core.SpatialMoveRemoval", TestFlags)
bool FReactiveSpatialTest::RunTest(const FString&)
{
    FSimulation Sim; auto A = Sim.Register(FMaterial::Metal(), FVector(-201,-201,0), 50);
    TestTrue(TEXT("Negative grid coordinates are queryable"), Sim.Query(FVector(-210,-210,0), 20).Contains(A));
    Sim.Move(A, FVector(1000,0,0));
    TestFalse(TEXT("Old grid cell no longer returns moved body"), Sim.Query(FVector(-210,-210,0), 20).Contains(A));
    TestTrue(TEXT("New cell contains body"), Sim.Query(FVector(1000,0,0), 20).Contains(A));
    Sim.Unregister(A); TestTrue(TEXT("Removed body has no state"), Sim.Find(A) == nullptr);
    const auto B = Sim.Register(FMaterial::Metal(), FVector::ZeroVector, 50);
    TestTrue(TEXT("Stale handles never alias new bodies"), A != B);
    FStimulus S; S.Target = A; TestFalse(TEXT("Stale target rejected"), Sim.Enqueue(S)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveAreaTest, "Reactive.Core.AreaBudgetAndValidation", TestFlags)
bool FReactiveAreaTest::RunTest(const FString&)
{
    FSimulation Sim; auto M = Isolated(FMaterial::Metal());
    const auto A = Sim.Register(M, FVector(-50,0,0), 30); const auto B = Sim.Register(M, FVector(50,0,0), 30);
    const double Before = Sim.Find(A)->EnthalpyJ + Sim.Find(B)->EnthalpyJ;
    FStimulus S; S.RadiusCm = 100; S.HeatJ = 1000; Sim.Enqueue(S); Sim.Step();
    TestTrue(TEXT("Area heat is shared, not multiplied by target count"), FMath::IsNearlyEqual(Before + 1000, Sim.Find(A)->EnthalpyJ + Sim.Find(B)->EnthalpyJ, 1.e-7));
    S.HeatJ = std::numeric_limits<double>::quiet_NaN(); TestFalse(TEXT("NaN input rejected"), Sim.Enqueue(S));
    S.HeatJ = 0; S.WaterKg = -1; TestFalse(TEXT("Negative water rejected"), Sim.Enqueue(S));
    TestEqual(TEXT("Invalid material rejected"), Sim.Register(FMaterial(), FVector::ZeroVector, -1), InvalidBody);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveSleepTest, "Reactive.Core.SleepWakeWeather", TestFlags)
bool FReactiveSleepTest::RunTest(const FString&)
{
    FSimulation Sim; const auto A = Sim.Register(FMaterial::Wood(), FVector::ZeroVector, 50);
    Steps(Sim, 30); TestEqual(TEXT("Stable body sleeps"), Sim.GetStats().Active, 0);
    Heat(Sim, A, 1000); TestEqual(TEXT("Input wakes sleeping body"), Sim.GetStats().Active, 1);
    FSimulation Rain; const auto B = Rain.Register(FMaterial::Wood(), FVector::ZeroVector, 50);
    Steps(Rain, 30); FEnvironment E; E.RainKgPerM2Sec = 0.1; Rain.SetEnvironment(E); Rain.Step();
    TestTrue(TEXT("Weather change wakes sleeping bodies"), Rain.Find(B)->WaterKg > 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveFractureTest, "Reactive.Core.FrozenBrittlenessAndBreakOnce", TestFlags)
bool FReactiveFractureTest::RunTest(const FString&)
{
    FSimulation Sim; auto M = Isolated(FMaterial::Wood()); M.InitialFuelKg = 0;
    const auto Wet = Sim.Register(M, FVector::ZeroVector, 50, 20, 0.02);
    const auto Dry = Sim.Register(M, FVector(1000,0,0), 50);
    Heat(Sim, Wet, -30000);
    FStimulus S; S.Target = Wet; S.ImpulseNs = FVector(10,0,0); Sim.Enqueue(S);
    S.Target = Dry; Sim.Enqueue(S); Sim.Step();
    TestTrue(TEXT("Frozen wet object fractures"), Sim.Find(Wet)->bBroken);
    TestFalse(TEXT("Same impulse does not fracture dry object"), Sim.Find(Dry)->bBroken);
    TestEqual(TEXT("Exactly one break event"), CountEvent(Sim.DrainEvents(), EEvent::Broken), 1);
    Steps(Sim, 5); TestEqual(TEXT("No repeated break event every tick"), CountEvent(Sim.DrainEvents(), EEvent::Broken), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactivePressureTest, "Reactive.Core.OpenFuelVersusSealedBurst", TestFlags)
bool FReactivePressureTest::RunTest(const FString&)
{
    FSimulation Sim; auto Open = Isolated(FMaterial::Oil()); auto Sealed = Open; Sealed.SealedVolumeM3 = 0.01;
    const auto A = Sim.Register(Open, FVector::ZeroVector, 50); const auto B = Sim.Register(Sealed, FVector(1000,0,0), 50);
    Heat(Sim, A, 60000); Heat(Sim, B, 60000);
    TestTrue(TEXT("Open oil burns"), Sim.Find(A)->bBurning);
    TestFalse(TEXT("Open oil does not automatically explode"), Sim.Find(A)->bBurst);
    TestTrue(TEXT("Confined gas energy raises pressure and bursts container"), Sim.Find(B)->bBurst);
    TestEqual(TEXT("One burst event"), CountEvent(Sim.DrainEvents(), EEvent::Burst), 1);
    Steps(Sim, 10); TestEqual(TEXT("Burst is irreversible and not retriggered"), CountEvent(Sim.DrainEvents(), EEvent::Burst), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveLimitTest, "Reactive.Core.BoundedQueuesAndElectricalBudget", TestFlags)
bool FReactiveLimitTest::RunTest(const FString&)
{
    FSettings Settings; Settings.MaxPendingInputs = 1; Settings.MaxBodies = 2; Settings.MaxElectricalNodes = 1;
    FSimulation Sim(Settings); const auto M = Isolated(FMaterial::Metal());
    const auto A = Sim.Register(M, FVector::ZeroVector, 50); Sim.Register(M, FVector(90,0,0), 50);
    TestEqual(TEXT("Registration budget enforced"), Sim.Register(M, FVector::ZeroVector, 50), InvalidBody);
    FStimulus S; S.Target = A; S.ElectricalJ = 1000;
    TestTrue(TEXT("First input accepted"), Sim.Enqueue(S)); TestFalse(TEXT("Input overflow rejected"), Sim.Enqueue(S));
    Sim.Step(); TestEqual(TEXT("Electrical visit budget enforced"), Sim.GetStats().ElectricalVisits, 1);
    TestTrue(TEXT("Truncated propagation is accounted as explicit loss"), FMath::IsNearlyEqual(Sim.GetStats().ElectricalDepositedJ + Sim.GetStats().ElectricalLostJ, 1000.0, 1.e-8));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveChainTest, "Reactive.Core.BurningWoodSpreadsAndCollapses", TestFlags)
bool FReactiveChainTest::RunTest(const FString&)
{
    FSimulation Sim; auto M = FMaterial::Wood(); M.CoolingWPerK = 0;
    const auto A = Sim.Register(M, FVector::ZeroVector, 50); const auto B = Sim.Register(M, FVector(110,0,0), 50);
    Heat(Sim, A, 100000); Steps(Sim, 600);
    TestTrue(TEXT("Source consumes fuel and loses structure"), Sim.Find(A)->bBroken);
    TestTrue(TEXT("Neighbor receives heat and consumes its own fuel"), Sim.Find(B)->FuelKg < M.InitialFuelKg);
    const auto Events = Sim.DrainEvents(); TestTrue(TEXT("Neighbor ignited without a spell-specific combo"), CountEvent(Events, EEvent::Ignited) >= 2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveWindTest, "Reactive.Core.WindBiasesHeatTransport", TestFlags)
bool FReactiveWindTest::RunTest(const FString&)
{
    FSimulation Sim; auto M = FMaterial::Metal(); M.CoolingWPerK = 0;
    Sim.Register(M, FVector::ZeroVector, 40, 100);
    const auto Up = Sim.Register(M, FVector(-200,0,0), 40, 20);
    const auto Down = Sim.Register(M, FVector(200,0,0), 40, 20);
    FEnvironment E; E.WindMPerSec = FVector(10,0,0); Sim.SetEnvironment(E); Sim.Step();
    TestTrue(TEXT("Downwind neighbor heats faster"), Sim.Find(Down)->TemperatureC > Sim.Find(Up)->TemperatureC);
    return true;
}
#endif
