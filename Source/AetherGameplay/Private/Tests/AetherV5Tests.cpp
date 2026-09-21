#include "Misc/AutomationTest.h"
#include "../AetherProgression.h"
#include "../AetherRules.h"
#include "ReactiveSimulation.h"
#if WITH_DEV_AUTOMATION_TESTS
using namespace Reactive;
namespace { constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
Reactive::FMaterial Inert(Reactive::FMaterial M){M.CoolingWPerK=0;M.ThermalCouplingWPerK=0;return M;}}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV5Receivers,"Aether.V5.UniqueReceiverCapacityAndLedger",Flags)
bool FV5Receivers::RunTest(const FString&)
{
 FSimulation S;auto A=S.Register(Inert(Reactive::FMaterial::Metal()),FVector(0,0,0),60);auto B=S.Register(Inert(Reactive::FMaterial::Metal()),FVector(100,0,0),60);auto C=S.Register(Inert(Reactive::FMaterial::Metal()),FVector(100,20,0),60);
 FElectricalReceiver Source;Source.LoadWeight=0;S.SetReceiver(A,Source);FElectricalReceiver Load;Load.Id=99;Load.LoadWeight=1;Load.CapacityJ=100;Load.HeatFraction=.1;Load.bTerminal=true;S.SetReceiver(B,Load);S.SetReceiver(C,Load);
 FStimulus E;E.Target=A;E.ElectricalJ=1000;E.RootCauseId=77;S.Enqueue(E);S.Step();int N=0;
 for(auto V:S.DrainEvents())if(V.Kind==EEvent::Shock){++N;TestEqual(TEXT("Cause"),V.RootCauseId,uint64(77));TestTrue(TEXT("Capacity before attenuation"),V.Magnitude<100);}
 TestEqual(TEXT("One exposure for two shapes"),N,1);const auto& T=S.GetStats();TestTrue(TEXT("Energy ledger"),FMath::IsNearlyEqual(T.ElectricalDepositedJ+T.ElectricalUsefulJ+T.ElectricalLostJ,1000.,1.e-6));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV5Replay,"Aether.V5.InputReplayRejectsSecondRelease",Flags)
bool FV5Replay::RunTest(const FString&)
{
 FSimulation S;auto A=S.Register(Inert(Reactive::FMaterial::Metal()),FVector::ZeroVector,10);FStimulus E;E.Target=A;E.InputId=35;E.ElectricalJ=50;
 TestTrue(TEXT("First"),S.Enqueue(E));TestFalse(TEXT("Duplicate queued"),S.Enqueue(E));S.Step();TestFalse(TEXT("Duplicate delivered"),S.Enqueue(E));TestEqual(TEXT("One budget"),S.GetStats().ElectricalInputJ,50.);
 E.Source=A;E.InputId=35^(uint64(A)*0x9e3779b97f4a7c15ULL);TestTrue(TEXT("Distinct source/sequence pair despite old XOR collision"),S.Enqueue(E));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV5Area,"Aether.V5.AreaPulseMergesReceiverOnce",Flags)
bool FV5Area::RunTest(const FString&)
{
 FSimulation S;FElectricalReceiver R;R.Id=12;R.LoadWeight=1;for(int I=0;I<4;++I){auto A=S.Register(Inert(Reactive::FMaterial::Metal()),FVector(I*30,0,0),20);S.SetReceiver(A,R);}
 FStimulus E;E.RadiusCm=200;E.ElectricalJ=100;S.Enqueue(E);S.Step();int N=0;for(auto V:S.DrainEvents())if(V.Kind==EEvent::Shock)++N;
 TestEqual(TEXT("One exposure"),N,1);TestEqual(TEXT("No multiplied budget"),S.GetStats().ElectricalInputJ,100.);
 FSimulation Scoped;auto Public=Scoped.Register(Inert(Reactive::FMaterial::Metal()),FVector::ZeroVector,10);auto Private=Scoped.Register(Inert(Reactive::FMaterial::Metal()),FVector(100,0,0),10);double Before=Scoped.Find(Private)->EnthalpyJ;
 Scoped.CanReceiveInput=[Private](FBodyId Target,FBodyId){return Target!=Private;};FStimulus Pulse;Pulse.RadiusCm=150;Pulse.HeatJ=100;Scoped.Enqueue(Pulse);Scoped.Step();TestEqual(TEXT("Private session ignores area input"),Scoped.Find(Private)->EnthalpyJ,Before);TestTrue(TEXT("Public target receives area input"),Scoped.Find(Public)->EnthalpyJ>Before);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV5RainDry,"Aether.V5.RainCannotReactivateDrainedElectricalWater",Flags)
bool FV5RainDry::RunTest(const FString&)
{
 FSimulation S;auto A=S.Register(Inert(Reactive::FMaterial::Water()),FVector::ZeroVector,60,20,0);auto Metal=Inert(Reactive::FMaterial::Metal());Metal.WaterCapacityKg=0;auto B=S.Register(Metal,FVector(100,0,0),60);
 FEnvironment Rain;Rain.RainKgPerM2Sec=.1;S.SetEnvironment(Rain);for(int I=0;I<8;++I)S.Step();TestTrue(TEXT("Thermal rain water"),S.Find(A)->WaterKg>0);TestEqual(TEXT("No eligible electrical water"),S.Find(A)->ElectricalWaterKg,0.);
 double Before=S.Find(B)->EnthalpyJ;FStimulus E;E.Target=A;E.ElectricalJ=100;S.Enqueue(E);S.Step();TestEqual(TEXT("No rain bridge"),S.Find(B)->EnthalpyJ,Before);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV5Terminal,"Aether.V5.CharacterEndpointCannotBridgeWater",Flags)
bool FV5Terminal::RunTest(const FString&)
{
 FSimulation S;auto A=S.Register(Inert(Reactive::FMaterial::Metal()),FVector(0,0,0),60);auto B=S.Register(Inert(Reactive::FMaterial::Metal()),FVector(100,0,0),60);auto C=S.Register(Inert(Reactive::FMaterial::Metal()),FVector(200,0,0),60);
 FElectricalReceiver R;R.bTerminal=true;R.LoadWeight=1;S.SetReceiver(B,R);double Before=S.Find(C)->EnthalpyJ;FStimulus E;E.Target=A;E.ElectricalJ=100;S.Enqueue(E);S.Step();TestEqual(TEXT("Terminal cannot forward"),S.Find(C)->EnthalpyJ,Before);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV5Cold,"Aether.V5.ExtractionFloorAccountsRejectedBudget",Flags)
bool FV5Cold::RunTest(const FString&)
{
 FSimulation S;auto A=S.Register(Inert(Reactive::FMaterial::Stone()),FVector::ZeroVector,10);double Before=S.Find(A)->EnthalpyJ;FStimulus E;E.Target=A;E.HeatJ=-1000000;S.Enqueue(E);S.Step();TestEqual(TEXT("Temperature floor"),S.Find(A)->TemperatureC,-200.);
 TestTrue(TEXT("Removed + rejected = requested"),FMath::IsNearlyEqual(Before-S.Find(A)->EnthalpyJ+S.GetStats().RejectedExtractionJ,1000000.,1.e-6));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV5Definitions,"Aether.V5.ContentDefinitionsValidate",Flags)
bool FV5Definitions::RunTest(const FString&)
{const auto& R=FAetherRules::Get();TestTrue(TEXT("Valid rules"),R.bValid);TestEqual(TEXT("Eight quests"),R.Quests.Num(),8);TestEqual(TEXT("Materials stack to 99"),FAetherProfile::MaxStack("Herb"),99);return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV5Dailies,"Aether.V5.DailyRewardAtomicAndRollover",Flags)
bool FV5Dailies::RunTest(const FString&)
{
 FAetherProfile P;P.CharacterId="DailyTest";P.Claims.Add(FName("Q_Main_08"));P.RefreshDaily("20260918");P.Add("Supply",2);
 TestTrue(TEXT("Supply"),P.ClaimDaily(0));TestFalse(TEXT("Idempotent"),P.ClaimDaily(0));TestEqual(TEXT("Gold once"),P.Gold,30);P.RefreshDaily("20260917");TestFalse(TEXT("Clock rollback cannot reset claim"),P.ClaimDaily(0));
 for(int I=0;I<3;++I)P.DailyEvidence.Add(*FString::Printf(TEXT("Patrol%d"),I));TestTrue(TEXT("Patrol"),P.ClaimDaily(1));P.RefreshDaily("20260919");TestTrue(TEXT("Rollover preserves story"),P.DailyClaims.IsEmpty()&&P.DailyEvidence.IsEmpty()&&P.Claims.Num()==1);
 while(P.Inventory.Num()<32)P.Add("TrainingSword",1);P.DailyEvidence={"DailyFire0","DailyFire1","DailyFire2"};for(auto& I:P.Inventory)if(I.DefinitionId=="Material")I.Count=99;
 int Gold=P.Gold;TestFalse(TEXT("Full bag keeps pending reward"),P.ClaimDaily(2));TestEqual(TEXT("No partial gold"),P.Gold,Gold);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV5Refinement,"Aether.V5.PathRefinementAndRegistrationOrder",Flags)
bool FV5Refinement::RunTest(const FString&)
{
 auto Delivered=[](int N,bool Reverse,double Length)
 {
  FSimulation S;TArray<FBodyId> IDs;IDs.SetNum(N+1);auto Metal=Inert(Reactive::FMaterial::Metal());
  for(int I=0;I<=N;++I){int Index=Reverse?N-I:I;IDs[Index]=S.Register(Metal,FVector(Length*Index/N,0,0),Length/N/2+.1);}
  FElectricalReceiver R;R.LoadWeight=.2/N;for(int I=0;I<N;++I)S.SetReceiver(IDs[I],R);R.LoadWeight=1;R.bTerminal=true;S.SetReceiver(IDs[N],R);
  FStimulus E;E.Target=IDs[0];E.ElectricalJ=1000;S.Enqueue(E);S.Step();double Result=0;for(auto V:S.DrainEvents())if(V.Body==IDs[N]&&V.Kind==EEvent::Shock)Result+=V.Magnitude;return Result;
 };
 double Coarse=Delivered(3,false,600),Fine=Delivered(6,true,600),Long=Delivered(6,false,900);
 TestTrue(TEXT("Conserved physical load under refinement"),Coarse>0&&FMath::Abs(Fine-Coarse)/Coarse<.05);TestTrue(TEXT("Longer path reduces delivery"),Long<Fine);return true;
}
#endif
