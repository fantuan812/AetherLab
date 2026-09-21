#include "Misc/AutomationTest.h"
#include "Persistence/AetherProfile.h"
#include "Persistence/AetherWorldState.h"
#include "World/AetherWorldDefinition.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace {constexpr auto V9Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
FAetherInventoryCommand Command(const FAetherProfile& P,FName Action,FGuid Item,int32 Q=1){FAetherInventoryCommand C;C.CommandId=FGuid::NewGuid();C.ExpectedInventoryRevision=P.Revision;C.Action=Action;C.ItemInstanceId=Item;C.Quantity=Q;return C;}}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV9Identity,"Aether.V9.InstanceSelectionAndStaleVersion",V9Flags)
bool FV9Identity::RunTest(const FString&)
{
 const auto& R=FAetherRules::Get();FAetherProfile P;P.Add("Potion",35);const auto First=P.Inventory[0].InstanceId,Second=P.Inventory[1].InstanceId;
 int32 N=0;auto C=Command(P,"Sell",Second,2);C.ShopId="Shop";
 TestTrue(TEXT("Sell selected second stack"),AetherItems::Prepare(P,C,R,N)==EAetherInventoryResult::Applied);
 TestEqual(TEXT("First identical stack untouched"),P.Inventory[0].Count,FAetherProfile::MaxStack("Potion"));
 TestEqual(TEXT("Exact quantity sold"),P.Count("Potion"),33);
 ++P.Revision;TestTrue(TEXT("Old view rejected"),AetherItems::Prepare(P,C,R,N)==EAetherInventoryResult::StaleRevision);
 TestEqual(TEXT("Stale request has no side effect"),P.Count("Potion"),33);
 C=Command(P,"Split",Second,3);TestTrue(TEXT("Split exact instance/quantity"),AetherItems::Prepare(P,C,R,N)==EAetherInventoryResult::Applied);
 TestEqual(TEXT("Quantity conserved"),P.Count("Potion"),33);TestEqual(TEXT("Split amount"),P.Inventory.Last().Count,3);
 auto Missing=Command(P,"Use",FGuid::NewGuid());TestTrue(TEXT("No fallback to an index"),AetherItems::Prepare(P,Missing,R,N)==EAetherInventoryResult::MissingInstance);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV9Merge,"Aether.V9.PartialMergeAndAtomicCapacity",V9Flags)
bool FV9Merge::RunTest(const FString&)
{
 auto R=FAetherRules::Get();R.InventoryCapacity=2;FAetherProfile P;P.Add("Potion",35,R);int32 N;
 auto C=Command(P,"Merge",P.Inventory[0].InstanceId,8);C.DestinationInstanceId=P.Inventory[1].InstanceId;
 TestTrue(TEXT("Partial merge fits available space"),AetherItems::Prepare(P,C,R,N)==EAetherInventoryResult::Applied);
 TestEqual(TEXT("Actual moved amount"),N,5);TestEqual(TEXT("Total conserved"),P.Count("Potion"),35);
 TestTrue(TEXT("Full slots still accept stack space"),P.Add("Potion",5,R));TestFalse(TEXT("All-or-nothing overflow"),P.Add("Potion",1,R));TestEqual(TEXT("Overflow preserved"),P.Count("Potion"),40);
 C=Command(P,"Split",P.Inventory[0].InstanceId,2);TestTrue(TEXT("Split full rejected"),AetherItems::Prepare(P,C,R,N)==EAetherInventoryResult::Capacity);
 TMap<FName,int32> Reward={{"Potion",1},{"Herb",2}};TestFalse(TEXT("Multi-item grant atomic"),AetherItems::Grant(P,Reward,10,R));TestEqual(TEXT("No partial gold"),P.Gold,0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV9Definition,"Aether.V9.NewConsumableShopLootDefinitions",V9Flags)
bool FV9Definition::RunTest(const FString&)
{
 auto R=FAetherRules::Get();auto Item=R.Items.FindChecked("Potion");Item.UseId="CustomHeal";R.Items.Add("TestElixir",Item);
 FAetherUseRule Use;Use.Health=17;R.Uses.Add("CustomHeal",Use);R.Shops.Add("TestMerchant",{"TestElixir"});R.LootTables.Add("TestDrop",{{"TestElixir",2}});
 FAetherProfile P;P.Gold=100;TestTrue(TEXT("New loot definition grants"),AetherItems::GrantTable(P,"TestDrop",R));
 auto C=Command(P,"Use",P.Inventory.Last().InstanceId);int32 N;
 TestEqual(TEXT("New use strategy data"),AetherItems::Use(P,C.ItemInstanceId,R)->Health,17.);
 TestTrue(TEXT("New consumable has no whitelist"),AetherItems::Prepare(P,C,R,N)==EAetherInventoryResult::Applied);
 C=Command(P,"Buy",FGuid());C.ShopId="TestMerchant";C.DefinitionId="TestElixir";TestTrue(TEXT("New shop candidate"),AetherItems::Prepare(P,C,R,N)==EAetherInventoryResult::Applied);
 C.ShopId="Unknown";TestTrue(TEXT("Unknown shop rejected"),AetherItems::Prepare(P,C,R,N)==EAetherInventoryResult::NotAllowed);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV9Repository,"Aether.V9.OfflineRecordsMigrationAndUniqueness",V9Flags)
bool FV9Repository::RunTest(const FString&)
{
 FReactiveSaveRecord A,B;A.StableId="A";B.StableId="B";A.Transform=FTransform(FVector(10,0,0));B.Transform=FTransform(FVector(20000,0,0));B.WaterKg=.37;
 TArray<FReactiveSaveRecord> Repo={A,B};A.Transform=FTransform(FVector(-8000,0,0));
 TestTrue(TEXT("Loaded-only capture merges"),AetherWorldState::Merge(Repo,{A}));
 TestEqual(TEXT("Offline object retained"),Repo.Num(),2);TestEqual(TEXT("Offline water retained"),Repo[1].WaterKg,.37);
 TestEqual(TEXT("Moved entity region migrates"),Repo[0].RegionId,AetherWorldState::RegionFor(FVector(-8000,0,0)));
 TestFalse(TEXT("Duplicate capture rejected atomically"),AetherWorldState::Merge(Repo,{A,A}));TestEqual(TEXT("Repository intact"),Repo.Num(),2);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV9Layout,"Aether.V9.ThirdSceneAndDefinitionValidation",V9Flags)
bool FV9Layout::RunTest(const FString&)
{
 const auto& D=FAetherWorldDefinitions::Get();TestTrue(TEXT("World definitions valid"),D.bValid);
 TestTrue(TEXT("Migration policy comes from data"),D.Find("FieldBucket")&&D.Find("FieldBucket")->bAllowAbsentFromOlderSave);
 const auto* Field=D.Find("FieldRod");const auto* Lab=D.Find("LabRod");const auto* Works=D.Find("MetalRod");
 TestTrue(TEXT("Same definition in three scenes"),Field&&Lab&&Works&&Field->Definition==Lab->Definition&&Lab->Definition==Works->Definition);
 FString Text;FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/WorldObjects.json")));
 Text.ReplaceInline(TEXT("\"WorksRope\""),TEXT("\"InvalidReference\""),ESearchCase::CaseSensitive); // rename both identity/link still valid
 TestTrue(TEXT("Stable IDs are content, not a whitelist"),FAetherWorldDefinitions::Parse(Text).bValid);
 Text.ReplaceInline(TEXT("\"Metal\""),TEXT("\"UnknownCapability\""));TestFalse(TEXT("Unknown capability rejected"),FAetherWorldDefinitions::Parse(Text).bValid);return true;
}
#endif
