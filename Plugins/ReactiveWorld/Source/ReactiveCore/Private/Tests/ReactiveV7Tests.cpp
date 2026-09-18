#include "ReactiveSimulation.h"
#include "Misc/AutomationTest.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS
using namespace Reactive;
namespace
{
constexpr auto V7Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
FMaterial InertV7(FMaterial M){M.CoolingWPerK=0;M.ThermalCouplingWPerK=0;return M;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV7Liquid,"Reactive.V7.LiquidCapacityAndEnthalpy",V7Flags)
bool FV7Liquid::RunTest(const FString&)
{
 FSimulation S;auto M=InertV7(FMaterial::Water()),Small=M;Small.WaterCapacityKg=.5;
 auto A=S.Register(M,FVector::ZeroVector,20,80,1),B=S.Register(Small,FVector(40,0,0),20,10,.1);
 const double H=S.Find(A)->EnthalpyJ+S.Find(B)->EnthalpyJ;
 TestTrue(TEXT("Only available destination space deducted"),FMath::IsNearlyEqual(S.TransferLiquid(A,B,1),.4,1.e-9));
 TestTrue(TEXT("Mass conserved"),FMath::IsNearlyEqual(S.Find(A)->WaterKg+S.Find(B)->WaterKg,1.1,1.e-9));
 TestTrue(TEXT("Source temperature enthalpy conserved"),FMath::IsNearlyEqual(S.Find(A)->EnthalpyJ+S.Find(B)->EnthalpyJ,H,1.e-7));
 const double Before=S.Find(A)->EnthalpyJ;TestEqual(TEXT("Full target rejects transfer"),S.TransferLiquid(A,B,1),0.);TestEqual(TEXT("No partial source debit"),S.Find(A)->EnthalpyJ,Before);
 TestTrue(TEXT("Hot water actually heats receiver"),S.Find(B)->TemperatureC>50);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV7Frozen,"Reactive.V7.PartialIceOnlyTransfersLiquid",V7Flags)
bool FV7Frozen::RunTest(const FString&)
{
 FSimulation S;auto M=InertV7(FMaterial::Water());auto A=S.Register(M,FVector::ZeroVector,20,20,1),B=S.Register(M,FVector(40,0,0),20,0,0);
 FState Half=*S.Find(A);Half.EnthalpyJ=FusionJPerKg*.5;TMap<FBodyId,FState> States;States.Add(A,Half);TestTrue(TEXT("Prepare half frozen"),S.RestoreStates(States));
 TestEqual(TEXT("Half kg liquid transferred"),S.TransferLiquid(A,B,1),.5);TestEqual(TEXT("Ice left in source"),S.Find(A)->IceFraction,1.);TestEqual(TEXT("No free extraction of ice"),S.WithdrawLiquid(A,1),0.);
 TestTrue(TEXT("Latent enthalpy remains balanced"),FMath::IsNearlyEqual(S.Find(A)->EnthalpyJ+S.Find(B)->EnthalpyJ,Half.EnthalpyJ,1.e-7));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV7Rain,"Reactive.V7.TransfersPreserveRainElectricalSeparation",V7Flags)
bool FV7Rain::RunTest(const FString&)
{
 for(double Initial:{0.,.2})
 {
  FSimulation S;auto M=InertV7(FMaterial::Water());auto A=S.Register(M,FVector::ZeroVector,50,20,Initial),B=S.Register(M,FVector(1000,0,0),20,20,0);
  FEnvironment Rain;Rain.RainKgPerM2Sec=.1;S.SetEnvironment(Rain);S.Step();const double Water=S.Find(A)->WaterKg,Electric=S.Find(A)->ElectricalWaterKg;
  const double Kg=Water*.5;S.TransferLiquid(A,B,Kg);
  TestTrue(TEXT("No electrical eligibility minted or lost by transfer"),FMath::IsNearlyEqual(S.Find(A)->ElectricalWaterKg+S.Find(B)->ElectricalWaterKg,Electric,1.e-10));
  TestTrue(TEXT("Uniform mixture carries its eligibility fraction"),FMath::IsNearlyEqual(S.Find(B)->ElectricalWaterKg,Electric*.5,1.e-10));
  if(Initial==0)TestEqual(TEXT("Rain-only pour does not cause electrical wetness"),S.Find(B)->ElectricalWetness01,0.);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV7Withdraw,"Reactive.V7.WithdrawalBoundaryLedger",V7Flags)
bool FV7Withdraw::RunTest(const FString&)
{
 FSimulation S;auto A=S.Register(InertV7(FMaterial::Water()),FVector::ZeroVector,20,80,1);const double Before=S.Find(A)->EnthalpyJ;
 TestEqual(TEXT("Withdraw amount"),S.WithdrawLiquid(A,.2),.2);
 TestTrue(TEXT("Exported enthalpy recorded"),FMath::IsNearlyEqual(Before-S.Find(A)->EnthalpyJ,S.GetStats().WithdrawnEnthalpyJ,1.e-7));
 TestTrue(TEXT("Exported mass recorded"),FMath::IsNearlyEqual(S.GetStats().WithdrawnWaterKg,.2,1.e-9));
 TestEqual(TEXT("Negative request rejected"),S.WithdrawLiquid(A,-1),0.);TestEqual(TEXT("NaN rejected"),S.WithdrawLiquid(A,std::numeric_limits<double>::quiet_NaN()),0.);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV7Steam,"Reactive.V7.VaporCarriesMixedElectricalWaterFraction",V7Flags)
bool FV7Steam::RunTest(const FString&)
{
 FSimulation S;auto M=InertV7(FMaterial::Water());auto A=S.Register(M,FVector::ZeroVector,20,20,1);
 FState Mixed=*S.Find(A);Mixed.ElectricalWaterKg=.25;TMap<FBodyId,FState> States;States.Add(A,Mixed);S.RestoreStates(States);
 FStimulus Heat;Heat.Target=A;Heat.HeatJ=FSimulation::InitialEnthalpy(M,100,1)+.2*VaporizationJPerKg-Mixed.EnthalpyJ;S.Enqueue(Heat);S.Step();
 TestTrue(TEXT("Vaporized mass"),FMath::IsNearlyEqual(S.Find(A)->WaterKg,.8,1.e-9));
 TestTrue(TEXT("No preferential evaporation of rain fraction"),FMath::IsNearlyEqual(S.Find(A)->ElectricalWaterKg,.2,1.e-9));
 TestTrue(TEXT("Vapor energy ledger"),FMath::IsNearlyEqual(Mixed.EnthalpyJ+Heat.HeatJ,S.Find(A)->EnthalpyJ+S.GetStats().VentedEnergyJ,1.e-6));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV7Cut,"Reactive.V7.CuttingIsNotImpulseOrHeat",V7Flags)
bool FV7Cut::RunTest(const FString&)
{
 FSimulation S;auto Rope=InertV7(FMaterial::Wood());Rope.CutResistanceJ=20;auto A=S.Register(Rope,FVector::ZeroVector,20);const double H=S.Find(A)->EnthalpyJ;
 FStimulus Cut;Cut.Target=A;Cut.CuttingWorkJ=10;Cut.InputId=1;S.Enqueue(Cut);S.Step();TestEqual(TEXT("Half integrity cut"),S.Find(A)->Integrity,.5);TestFalse(TEXT("Repeated cutting input rejected"),S.Enqueue(Cut));
 Cut.InputId=2;Cut.CuttingWorkJ=11;S.Enqueue(Cut);S.Step();TestTrue(TEXT("Cut severs rope"),S.Find(A)->bBroken);TestEqual(TEXT("No invented heat"),S.Find(A)->EnthalpyJ,H);
 int Broken=0,Impulse=0;for(auto E:S.DrainEvents()){Broken+=E.Kind==EEvent::Broken;Impulse+=E.Kind==EEvent::Impulse;}
 TestEqual(TEXT("Exactly one break"),Broken,1);TestEqual(TEXT("Cut produces no push impulse"),Impulse,0);
 TestEqual(TEXT("Work partition balanced"),S.GetStats().CuttingDeliveredJ,S.GetStats().CuttingAbsorbedJ+S.GetStats().CuttingUnusedJ);
 const auto Stone=S.Register(InertV7(FMaterial::Stone()),FVector(500,0,0),20);Cut.Target=Stone;Cut.InputId=3;Cut.CuttingWorkJ=100;S.Enqueue(Cut);S.Step();TestEqual(TEXT("Non-cuttable material ignores blade work"),S.Find(Stone)->Integrity,1.);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV7InvalidCut,"Reactive.V7.InvalidCuttingInputIsAtomic",V7Flags)
bool FV7InvalidCut::RunTest(const FString&)
{
 FSimulation S;auto A=S.Register(InertV7(FMaterial::Wood()),FVector::ZeroVector,20);const double H=S.Find(A)->EnthalpyJ;
 for(double Bad:{-1.,std::numeric_limits<double>::quiet_NaN(),1.e7}){FStimulus X;X.Target=A;X.CuttingWorkJ=Bad;X.HeatJ=1000;TestFalse(TEXT("Reject whole mixed stimulus"),S.Enqueue(X));}
 S.Step();TestEqual(TEXT("No partial heat applied"),S.Find(A)->EnthalpyJ,H);auto Invalid=FMaterial::Wood();Invalid.CutResistanceJ=-1;TestFalse(TEXT("Invalid material cut resistance"),Invalid.IsValid());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV7TransferGate,"Reactive.V7.LiquidTransferAuthorizationIsAtomic",V7Flags)
bool FV7TransferGate::RunTest(const FString&)
{
 FSimulation S;auto M=InertV7(FMaterial::Water());auto A=S.Register(M,FVector::ZeroVector,20,20,1),B=S.Register(M,FVector(40,0,0),20);
 S.CanReceiveInput=[B](FBodyId Target,FBodyId){return Target!=B;};TestEqual(TEXT("Private target rejected"),S.TransferLiquid(A,B,.5,A),0.);TestEqual(TEXT("No debit on rejection"),S.Find(A)->WaterKg,1.);
 S.CanReceiveInput=nullptr;S.CanExchange=[](FBodyId,FBodyId){return false;};TestEqual(TEXT("Occluded pipe rejected"),S.TransferLiquid(A,B,.5,A),0.);
 S.CanExchange=nullptr;TestEqual(TEXT("Real transfer"),S.TransferLiquid(A,B,.5,A),.5);TestEqual(TEXT("Credits actual operator"),S.Find(B)->LastSource,A);TestTrue(TEXT("New causal chain"),S.Find(B)->RootCauseId!=0);return true;
}
#endif
