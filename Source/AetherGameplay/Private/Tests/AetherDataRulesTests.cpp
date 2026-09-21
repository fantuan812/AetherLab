#include "Misc/AutomationTest.h"
#include "Framework/AetherProgression.h"
#include "Inventory/AetherInventoryRules.h"
#include "Quests/AetherQuestRuntime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
constexpr auto DataFlags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
FString FixtureText(){FString Text;FFileHelper::LoadFileToString(Text,*(FPaths::ProjectDir()/TEXT("Docs/Fixtures/V802.rules.json")));return Text;}
FAetherRules Fixture(){return FAetherRules::Parse(FixtureText());}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDataQuest,"Aether.V802.ReorderedNinthQuestAndRewardPolicy",DataFlags)
bool FDataQuest::RunTest(const FString&)
{
 const auto R=Fixture();if(!TestTrue(TEXT("Fixture loads reversed graph"),R.bValid))return false;
 TestEqual(TEXT("Ninth quest allowed"),R.Quests.Num(),9);TestEqual(TEXT("Ninth is first in presentation order"),R.Quests[0].Id,FName("Q_Test_09"));
 FAetherWorldFacts Facts;Facts.Record("SupplyRestored","Pump",R);FAetherProfile P;
 AetherQuests::Settle(P,Facts,false,R);TestTrue(TEXT("Early world result does not skip prerequisites"),P.Evidence.IsEmpty()&&P.Claims.IsEmpty());
 for(FName Id:{FName("Q_Main_01"),FName("Q_Main_02"),FName("Q_Main_03")})
 {for(FName Objective:R.Quest(Id)->Objectives)P.Observe(Objective,R);AetherQuests::Settle(P,Facts,false,R);}
 TestTrue(TEXT("Registration effect follows stable definition"),P.bRegistered);
 TestTrue(TEXT("Prior public supply result follows newly unlocked quest"),P.Claims.Contains("Q_Main_05")&&P.Count("TideStaff")==1);
 TestTrue(TEXT("Ninth manual quest stays pending"),P.Complete("Q_Test_09",R)&&!P.Claims.Contains("Q_Test_09"));
 const int32 Gold=P.Gold;TestTrue(TEXT("Manual claim includes ninth definition"),AetherQuests::Settle(P,Facts,true,R));
 TestTrue(TEXT("Defined reward paid once"),P.Gold==Gold+15&&P.Count("SurveySword")==1&&P.Claims.Contains("Q_Test_09"));
 TestFalse(TEXT("Repeating claim cannot pay twice"),AetherQuests::Settle(P,Facts,true,R));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDataBag,"Aether.V802.FullBagRetainsPendingReward",DataFlags)
bool FDataBag::RunTest(const FString&)
{
 const auto R=Fixture();FAetherProfile P;P.Claims={"Q_Main_01","Q_Main_02","Q_Main_03"};
 for(int I=0;I<32;++I)P.Add("TrainingSword",1,R);
 FAetherWorldFacts Facts;Facts.Record("SupplyRestored","Pump",R);AetherQuests::Settle(P,Facts,false,R);
 TestTrue(TEXT("Full bag records objective but not reward"),P.Complete("Q_Main_05",R)&&P.Gold==0&&P.Count("TideStaff")==0);
 P.Remove("TrainingSword",1,R);AetherQuests::Settle(P,Facts,true,R);
 TestTrue(TEXT("First freed slot pays supply only"),P.Gold==60&&P.Count("TideStaff")==1&&!P.Claims.Contains("Q_Test_09"));
 P.Remove("TrainingSword",1,R);AetherQuests::Settle(P,Facts,true,R);
 TestTrue(TEXT("Next freed slot pays manual ninth once"),P.Gold==75&&P.Count("SurveySword")==1&&P.Validate(R));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDataEquipment,"Aether.V802.NewItemEquipmentAndProfileRoundtrip",DataFlags)
bool FDataEquipment::RunTest(const FString&)
{
 const auto R=Fixture();FAetherProfile P;P.CharacterId="DataFixture";
 TestTrue(TEXT("New definition stacks without whitelist"),P.Add("SurveySword",1,R));const FGuid Id=P.Inventory.Last().InstanceId;
 TestTrue(TEXT("New item equips by capability"),P.Equip(Id,R)&&P.Validate(R));
 TArray<FAetherEquippedSlot> Out;TestTrue(TEXT("Catalog reference resolves independently of inventory ID"),AetherInventory::BuildLoadout(P,R,Out)&&Out.Num()==1&&Out[0].ItemId=="TrainingSword");
 TestTrue(TEXT("Runtime ownership follows catalog reference"),AetherInventory::OwnsEquipment(P,"TrainingSword",R));
 TArray<uint8> Bytes;FMemoryWriter Writer(Bytes,true);FObjectAndNameAsStringProxyArchive Save(Writer,false);bool W=false;P.NetSerialize(Save,nullptr,W);
 FMemoryReader Reader(Bytes,true);FObjectAndNameAsStringProxyArchive Load(Reader,false);FAetherProfile Restored;bool L=false;Restored.NetSerialize(Load,nullptr,L);
 TestTrue(TEXT("Profile serializer retains alias definition and owned instance"),W&&L&&Restored.Validate(R)&&Restored.Equipped.FindRef("MainHand")==Id&&Restored.Count("SurveySword")==1);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDataSlots,"Aether.V802.AtomicSlotConflictsAndItemPermissions",DataFlags)
bool FDataSlots::RunTest(const FString&)
{
 const auto R=Fixture();FAetherProfile P;P.Add("TrainingSword",1,R);P.Equip(P.Inventory.Last().InstanceId,R);
 P.Add("TrainingShield",1,R);const auto Shield=P.Inventory.Last().InstanceId;P.Equip(Shield,R);
 P.Add("TrainingHammer",1,R);const auto Hammer=P.Inventory.Last().InstanceId;
 TestTrue(TEXT("Larger item clears conflicting secondary slot"),P.Equip(Hammer,R)&&!P.Equipped.Contains("OffHand"));const auto Before=P.Equipped;
 TestFalse(TEXT("Secondary insertion into occupied slot rejected"),P.Equip(Shield,R));TestTrue(TEXT("Rejected change is atomic"),P.Equipped.OrderIndependentCompareEqual(Before));
 P.Add("BellHammer",1,R);TestFalse(TEXT("Enemy-only capability rejects player equip"),P.Equip(P.Inventory.Last().InstanceId,R));
 P.Add("SurveyToken",1,R);TestFalse(TEXT("New protected definition cannot be removed"),P.Remove("SurveyToken",1,R));
 auto Bad=P;Bad.Equipped.Add("OffHand",Shield);TArray<FAetherEquippedSlot> Out;AetherInventory::BuildLoadout(P,R,Out);
 TestFalse(TEXT("Whole conflicting loadout is invalid"),Bad.Validate(R));TestFalse(TEXT("Invalid loadout produces no replacement configuration"),AetherInventory::BuildLoadout(Bad,R,Out));
 TestTrue(TEXT("Previously valid output preserved"),Out.Num()==1&&Out[0].ItemId=="TrainingHammer");return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDataFacts,"Aether.V802.WorldFactsCannotForgePrivateOrDailyProgress",DataFlags)
bool FDataFacts::RunTest(const FString&)
{
 const auto R=Fixture();FAetherWorldFacts Facts;
 TestFalse(TEXT("Untrusted source rejected"),Facts.Record("SupplyRestored","Bucket0",R));
 TestFalse(TEXT("Personal training cannot enter public ledger"),Facts.Record("TrainingExtinguished","Training_Other",R));
 TestFalse(TEXT("Daily cannot enter public ledger"),Facts.Record("DailyFire0","Commission_Other",R));
 TestTrue(TEXT("Trusted alternative pump accepted"),Facts.Record("SupplyRestored","PowerReceiver",R)&&Facts.Validate(R));
 auto Forged=Facts;Forged.Sources.Add("TrainingExtinguished","Pump");TestFalse(TEXT("Forged stored personal fact rejected"),Forged.Validate(R));
 FAetherProfile P;P.Claims={"Q_Main_01","Q_Main_02"};P.DailyDate="20260918";AetherQuests::Settle(P,Facts,false,R);
 TestTrue(TEXT("Private and daily progress stays independent"),P.Evidence.IsEmpty()&&P.DailyEvidence.IsEmpty()&&!P.Claims.Contains("Q_Main_03"));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDataValidation,"Aether.V802.DefinitionCyclesAndUnsafePoliciesRejected",DataFlags)
bool FDataValidation::RunTest(const FString&)
{
 auto Mutate=[&](TFunction<void(TSharedPtr<FJsonObject>)> Change){TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FixtureText()),Root);Change(Root);FString Text;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));return FAetherRules::Parse(Text).bValid;};
 TestFalse(TEXT("Self cycle rejected independent of order"),Mutate([](auto Root){Root->GetArrayField(TEXT("Quests"))[0]->AsObject()->SetArrayField(TEXT("Prerequisites"),{MakeShared<FJsonValueString>(TEXT("Q_Test_09"))});}));
 TestFalse(TEXT("Dangling prerequisite rejected"),Mutate([](auto Root){Root->GetArrayField(TEXT("Quests"))[0]->AsObject()->SetArrayField(TEXT("Prerequisites"),{MakeShared<FJsonValueString>(TEXT("Missing"))});}));
 TestFalse(TEXT("Personal objective cannot opt into world replay"),Mutate([](auto Root){Root->GetObjectField(TEXT("Objectives"))->GetObjectField(TEXT("TrainingExtinguished"))->SetBoolField(TEXT("Retroactive"),true);}));
 TestFalse(TEXT("Fractional rewards rejected rather than truncated"),Mutate([](auto Root){Root->GetArrayField(TEXT("Quests"))[0]->AsObject()->SetNumberField(TEXT("Gold"),1.5);}));
 TestFalse(TEXT("Equipment must occupy its primary slot"),Mutate([](auto Root){Root->GetObjectField(TEXT("Items"))->GetObjectField(TEXT("SurveySword"))->SetArrayField(TEXT("OccupiedSlots"),{});}));return true;
}
#endif
