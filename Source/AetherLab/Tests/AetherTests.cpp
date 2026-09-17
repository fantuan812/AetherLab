#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "ReactiveSimulation.h"
#include "../AetherCombat.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherWaterTransfer, "Reactive.Adventure.LiquidTransferConservesMassAndEnthalpy", Flags)
bool FAetherWaterTransfer::RunTest(const FString&)
{
    Reactive::FSimulation Sim; auto M=Reactive::FMaterial::Water(); M.CoolingWPerK=0; M.ThermalCouplingWPerK=0;
    const auto A=Sim.Register(M,FVector::ZeroVector,50,60,1),B=Sim.Register(M,FVector(100,0,0),50,0,.2);
    const double BeforeH=Sim.Find(A)->EnthalpyJ+Sim.Find(B)->EnthalpyJ;
    TestEqual(TEXT("Transfer bounded quantity"),Sim.TransferLiquid(A,B,.3),.3);
    TestTrue(TEXT("Mass conserved"),FMath::IsNearlyEqual(Sim.Find(A)->WaterKg+Sim.Find(B)->WaterKg,1.2,1.e-9));
    TestTrue(TEXT("Enthalpy conserved"),FMath::IsNearlyEqual(Sim.Find(A)->EnthalpyJ+Sim.Find(B)->EnthalpyJ,BeforeH,1.e-6));
    Sim.CanExchange=[](uint32,uint32){return false;}; TestEqual(TEXT("Blocked channel rejects flow"),Sim.TransferLiquid(A,B,.2),0.0);
    Sim.CanExchange=nullptr; Reactive::FStimulus Cold; Cold.Target=A; Cold.HeatJ=-1.e6; Sim.Enqueue(Cold); Sim.Step();
    TestEqual(TEXT("Frozen source cannot flow"),Sim.TransferLiquid(A,B,.1),0.0);
    TestEqual(TEXT("Frozen source cannot fill inventory"),Sim.WithdrawLiquid(A,.1),0.0);
    TestEqual(TEXT("Negative withdrawal rejected"),Sim.WithdrawLiquid(B,-1),0.0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSaveTransaction, "Reactive.Adventure.RestoreIsAtomicAndClearsTransientInput", Flags)
bool FAetherSaveTransaction::RunTest(const FString&)
{
    Reactive::FSimulation Sim; auto M=Reactive::FMaterial::Water(); M.CoolingWPerK=0; M.ThermalCouplingWPerK=0;
    auto A=Sim.Register(M,FVector::ZeroVector,30,-10,.5),B=Sim.Register(M,FVector(1000,0,0),30,20,.5);
    TMap<uint32,Reactive::FState> Save; Save.Add(A,*Sim.Find(A)); Save.Add(B,*Sim.Find(B));
    auto Invalid=Save; Invalid[A].EnthalpyJ+=1000; Invalid[B].WaterKg=999;
    TestFalse(TEXT("Invalid batch rejected"),Sim.RestoreStates(Invalid));
    TestEqual(TEXT("Other body unchanged after rejection"),Sim.Find(A)->EnthalpyJ,Save[A].EnthalpyJ);
    Reactive::FStimulus Pending; Pending.Target=A; Pending.HeatJ=100000; Sim.Enqueue(Pending);
    TestTrue(TEXT("Valid batch accepted"),Sim.RestoreStates(Save)); Sim.Step();
    TestEqual(TEXT("Pending heat does not leak into restored world"),Sim.Find(A)->EnthalpyJ,Save[A].EnthalpyJ);
    TestEqual(TEXT("Ice re-derived from enthalpy"),Sim.Find(A)->IceFraction,1.0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherCausalElectric, "Reactive.Adventure.ConductionPreservesAttribution", Flags)
bool FAetherCausalElectric::RunTest(const FString&)
{
    Reactive::FSimulation Sim; auto M=Reactive::FMaterial::Metal();
    const auto Source=Sim.Register(M,FVector(2000,0,0),20);
    const auto A=Sim.Register(M,FVector::ZeroVector,60);
    Sim.Register(M,FVector(100,0,0),60); Sim.Register(M,FVector(200,0,0),60);
    Reactive::FStimulus S; S.Source=Source; S.Target=A; S.ElectricalJ=6000; Sim.Enqueue(S); Sim.Step();
    int32 Count=0;
    for (const Reactive::FEvent& E:Sim.DrainEvents()) if (E.Kind==Reactive::EEvent::Shock) { ++Count; TestEqual(TEXT("Original caster retained"),E.Source,Source); }
    TestEqual(TEXT("Three graph nodes receive shock"),Count,3);
    TestTrue(TEXT("Electrical energy still conserved"),FMath::IsNearlyEqual(Sim.GetStats().ElectricalDepositedJ+Sim.GetStats().ElectricalLostJ,6000.0,1.e-6));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherProjectileWeather, "Reactive.Adventure.WeatherChangesProjectileEnergyAndTrajectory", Flags)
bool FAetherProjectileWeather::RunTest(const FString&)
{
    double DryHeat=60000,WetHeat=60000; FVector DryV(1300,0,0),WetV=DryV;
    Reactive::FEnvironment Dry,Wet; Wet.RainKgPerM2Sec=.1; Wet.WindMPerSec=FVector(0,10,0);
    AAetherProjectile::IntegrateWeather(DryHeat,DryV,Dry,1); AAetherProjectile::IntegrateWeather(WetHeat,WetV,Wet,1);
    TestTrue(TEXT("Rain removes more energy"),WetHeat<DryHeat);
    TestTrue(TEXT("Crosswind bends trajectory"),WetV.Y>0&&DryV.Y==0);
    AAetherProjectile::IntegrateWeather(WetHeat,WetV,Wet,20);
    TestEqual(TEXT("Heat is bounded at zero"),WetHeat,0.0); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherScaleBudget, "Reactive.Adventure.Scale4096Bodies64Active", Flags)
bool FAetherScaleBudget::RunTest(const FString&)
{
    Reactive::FSimulation Sim; auto M=Reactive::FMaterial::Stone(); M.CoolingWPerK=0;
    TArray<uint32> Ids;
    for (int32 I=0;I<4096;++I) Ids.Add(Sim.Register(M,FVector((I%64)*1000,(I/64)*1000,0),10));
    for (int32 I=0;I<22;++I) Sim.Step();
    TestEqual(TEXT("Stable objects sleep"),Sim.GetStats().Active,0);
    for (int32 I=0;I<64;++I) { Reactive::FStimulus S; S.Target=Ids[I*64]; S.HeatJ=10000; Sim.Enqueue(S); }
    Sim.Step(); TArray<double> Times;
    for (int32 I=0;I<100;++I) { const double Start=FPlatformTime::Seconds(); Sim.Step(); Times.Add((FPlatformTime::Seconds()-Start)*1000); Sim.DrainEvents(); }
    Times.Sort();
    UE_LOG(LogTemp,Display,TEXT("AETHER_BENCHMARK bodies=%d active=%d samples=100 p50_ms=%.4f p95_ms=%.4f budget_hits=%llu workload=sparse_core_no_render_no_network"),Sim.GetStats().Registered,Sim.GetStats().Active,Times[50],Times[95],Sim.GetStats().BudgetHits);
    TestEqual(TEXT("Only stimulated region stays active"),Sim.GetStats().Active,64);
    TestEqual(TEXT("No budget exhaustion"),Sim.GetStats().BudgetHits,uint64(0));
    return true;
}
#endif
