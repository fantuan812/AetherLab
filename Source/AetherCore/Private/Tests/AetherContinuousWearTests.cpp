#include "Misc/AutomationTest.h"
#include "Combat/AetherEquipmentMath.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherContinuousWearTest,"Aether.V10.Equipment.ContinuousHeatWearFrameInvariance",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherContinuousWearTest::RunTest(const FString&)
{
 // 相同十秒、五点每秒的有效伤害，改变分帧方式仍必须产生五十次磨损。
 for(const int32 FPS:{30,60,120}){
  double Remainder=0;int32 Count=0;
  for(int32 Frame=0;Frame<FPS*10;++Frame)Count+=AetherEquipmentMath::ContinuousWear(5.f/FPS,Remainder);
  TestEqual(FString::Printf(TEXT("%d FPS conserves total wear"),FPS),Count,50);
  TestTrue(TEXT("Remainder bounded"),Remainder>=0&&Remainder<1);
 }
 double Remainder=0;
 TestEqual(TEXT("Sub-point exposure does not round up every frame"),AetherEquipmentMath::ContinuousWear(.25f,Remainder),0);
 TestEqual(TEXT("Fraction survives until the next exposure"),AetherEquipmentMath::ContinuousWear(.75f,Remainder),1);
 TestEqual(TEXT("Invalid negative damage does not refund wear"),AetherEquipmentMath::ContinuousWear(-1,Remainder),0);
 return true;
}
#endif
