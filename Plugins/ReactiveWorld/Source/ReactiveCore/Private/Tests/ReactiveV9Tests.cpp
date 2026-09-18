#include "ReactiveSimulation.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV9PartialRestore,"Reactive.V9.PartialRestoreRetainsLiveDeduplication",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV9PartialRestore::RunTest(const FString&)
{
 Reactive::FSimulation S;auto Material=Reactive::FMaterial::Stone();
 auto A=S.Register(Material,FVector::ZeroVector,50),B=S.Register(Material,FVector(5000,0,0),50);
 Reactive::FStimulus Input;Input.InputId=901;Input.Target=A;Input.HeatJ=10;
 TestTrue(TEXT("Initial stimulus accepted"),S.Enqueue(Input));S.Step();
 TMap<Reactive::FBodyId,Reactive::FState> Restore;Restore.Add(B,*S.Find(B));
 TestTrue(TEXT("Restore a different loaded region"),S.RestoreStates(Restore,true));
 TestFalse(TEXT("Old live input still deduplicated"),S.Enqueue(Input));
 Input.InputId=902;TestTrue(TEXT("Fresh live input accepted"),S.Enqueue(Input));
 TestFalse(TEXT("Pending live stimulus prevents partial restore"),S.RestoreStates(Restore,true));return true;
}
#endif
