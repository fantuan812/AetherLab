#include "Misc/AutomationTest.h"
#include "Commands/AetherServerFactCoordinator.h"
#include "Definitions/AetherV10Definitions.h"
#include "Persistence/AetherSqliteStore.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherWorldCodec.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherWearFactBatchTest,"Aether.V10.Equipment.QueuedWearBatchPreservesCountsAndInFlightIdentity",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherWearFactBatchTest::RunTest(const FString&)
{
 const auto& D=FAetherV10Definitions::Get();FString Why;
 FAetherProfileStateV10 P;P.CharacterId=TEXT("WearOwner");FAetherWorldStateV10 W;
 FAetherV10ItemInstance Item;Item.InstanceId=FGuid::NewGuid();Item.DefinitionId=TEXT("IronCuirass");
 Item.Quantity=1;Item.SlotIndex=0;Item.Durability=D.Items.Items.FindChecked(Item.DefinitionId).MaxDurability;P.Inventory.Items.Add(Item);
 FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/WearBatch")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
 auto DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Isolated real SQLite store"),DB.Store.IsValid()))return false;
 ON_SCOPE_EXIT {DB.Store->Close();};
 FAetherTransaction Seed;Seed.ActorId=P.CharacterId;Seed.ExpectedProfileRevision=-1;Seed.CommandId=AetherTransactions::NewCommandId(-1);Seed.Request={1};
 FAetherAggregateWrite Row;Row.Value.Key={EAetherAggregateKind::Profile,P.CharacterId};
 if(!TestTrue(TEXT("Valid profile"),AetherProfileCodec::Encode(P,D.Items,D.Skills,D.Rules,Row.Value.Payload,Why)))return false;Seed.Writes.Add(Row);
 Row.Value.Key={EAetherAggregateKind::World,TEXT("Main")};
 if(!TestTrue(TEXT("Valid world"),AetherWorldCodec::Encode(W,D.Items,D.Rules,{{P.CharacterId,0}},Row.Value.Payload,Why)))return false;Seed.Writes.Add(Row);
 if(!TestTrue(TEXT("Seed commits"),DB.Store->Commit(Seed).Get().Code==EAetherStoreCode::Committed))return false;
 FAetherServerFactCoordinator Facts(DB.Store.ToSharedRef());FAetherServerFact E;
 E.Kind=EAetherServerFactKind::EquipmentWear;E.CharacterId=P.CharacterId;E.FactId=TEXT("EquipmentWear");E.WornItems={Item.InstanceId};
 for(int32 N=0;N<30;++N)if(!TestTrue(TEXT("All accepted wear retained"),Facts.Enqueue(E,Why)))return false;
 TestEqual(TEXT("Queued adjacent events coalesce"),Facts.PendingCount(),1);
 Facts.Poll(FPlatformTime::Seconds()); // 第一批已开始读取，此后不得修改它的请求和序号。
 for(int32 N=0;N<20;++N)if(!TestTrue(TEXT("New events accepted behind in-flight batch"),Facts.Enqueue(E,Why)))return false;
 TestEqual(TEXT("In-flight batch remains separate"),Facts.PendingCount(),2);
 TArray<FAetherServerFactCompletion> Done;const double Deadline=FPlatformTime::Seconds()+5;
 while(Facts.PendingCount()&&FPlatformTime::Seconds()<Deadline){Done.Append(Facts.Poll(FPlatformTime::Seconds()));FPlatformProcess::Sleep(.001f);}
 if(!TestEqual(TEXT("Both batches committed"),Done.Num(),2)||!TestTrue(TEXT("Committed readback"),Done.Last().Profile.IsSet()))return false;
 const auto& Final=Done.Last().Profile.GetValue();
 TestEqual(TEXT("No wear events lost or applied twice"),Final.Inventory.Find(Item.InstanceId)->Durability,Item.Durability-50);
 TestEqual(TEXT("One durable cursor per immutable batch"),Final.WearSequence,int64(2));
 E.WearCount=1000001;TestFalse(TEXT("Overflowing batch rejected"),Facts.Enqueue(E,Why));
 E.WearCount=1;for(int32 N=0;N<2000;++N)if(!Facts.Enqueue(E,Why)){AddError(TEXT("Burst exceeded bounded job queue"));return false;}
 TestEqual(TEXT("Large continuous burst remains one bounded job"),Facts.PendingCount(),1);
 Done.Reset();const double End=FPlatformTime::Seconds()+5;
 while(Facts.PendingCount()&&FPlatformTime::Seconds()<End){Done.Append(Facts.Poll(FPlatformTime::Seconds()));FPlatformProcess::Sleep(.001f);}
 if(!TestTrue(TEXT("Burst completes"),Done.Num()==1&&Done[0].Profile.IsSet()))return false;
 TestEqual(TEXT("Saturated wear cannot make durability negative"),Done[0].Profile->Inventory.Find(Item.InstanceId)->Durability,0);
 return true;
}
#endif
