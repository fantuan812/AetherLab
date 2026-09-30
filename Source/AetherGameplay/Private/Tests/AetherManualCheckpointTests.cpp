#include "Misc/AutomationTest.h"
#include "Persistence/AetherManualWorldSave.h"
#include "Persistence/AetherSqliteStore.h"
#include "Definitions/AetherV10Definitions.h"
#include "World/AetherWorldCodec.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherManualCheckpointTest,"Aether.Systems.Persistence.ManualCheckpointReopen",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherManualCheckpointTest::RunTest(const FString&)
{
    const auto& D=FAetherV10Definitions::Get();FString Why;
    if(!TestTrue(TEXT("Definitions ready"),D.bValid))return false;
    FAetherSqliteOptions Options;Options.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/ManualCheckpoint")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    auto Opened=AetherSQLite::Open(Options);
    if(!TestTrue(TEXT("Open isolated checkpoint store"),Opened.Store.IsValid()))return false;
    FAetherWorldStateV10 Original;FAetherReactiveRecordV10 Body;Body.StableId=TEXT("Checkpoint.Body");Body.RegionId=TEXT("Town");Original.Bodies.Add(Body);
    FAetherStoredAggregate Initial;Initial.Key={EAetherAggregateKind::World,TEXT("Main")};
    if(!TestTrue(TEXT("Encode initial world"),AetherWorldCodec::Encode(Original,D.Items,D.Rules,{},Initial.Payload,Why)))return false;
    if(!TestTrue(TEXT("Initialize world"),Opened.Store->InitializeWorld(Initial).Get().Code==EAetherStoreCode::Committed))return false;
    // The manual request's Future is completed by the same real checkpoint
    // state machine used inside UAetherNativePersistence::SaveLoadedPhysics.
    // A DTO capture adapter isolates durability from rendered physics assembly.
    FAetherWorldCheckpoint Checkpoint(Opened.Store.ToSharedRef());FAetherManualWorldSave Request;
    TPromise<FAetherWorldCheckpointResult> Promise;
    const FVector SavedPosition(123,456,789);
    if(!TestTrue(TEXT("Start checkpoint capture"),Checkpoint.Start([&](const auto& Previous,auto& Candidate,FString&){
        Candidate=Previous;Candidate.Bodies[0].Transform.SetLocation(SavedPosition);Candidate.Bodies[0].WaterKg=.5;return true;
    },Why)))return false;
    Request.Start([&]{return Promise.GetFuture();});
    TestFalse(TEXT("Manual request never acknowledges pending write"),Request.Poll().IsSet());
    const double Deadline=FPlatformTime::Seconds()+5;
    while(Checkpoint.Phase()!=EAetherWorldCheckpointPhase::Complete&&Checkpoint.Phase()!=EAetherWorldCheckpointPhase::Failed&&FPlatformTime::Seconds()<Deadline)
    {Checkpoint.Poll();FPlatformProcess::Sleep(.001f);}
    auto Outcome=Checkpoint.Result();
    TestTrue(TEXT("Real checkpoint commits"),Checkpoint.Phase()==EAetherWorldCheckpointPhase::Complete&&Outcome.Code==EAetherStoreCode::Committed&&Outcome.World.IsSet());
    Promise.SetValue(MoveTemp(Outcome));const auto Feedback=Request.Poll();
    TestTrue(TEXT("Committed revision appears in manual feedback"),Feedback.IsSet()&&Feedback->Contains(TEXT("修订 1")));
    Opened.Store->Close();Opened.Store.Reset();Opened=AetherSQLite::Open(Options);
    if(!TestTrue(TEXT("Reopen saved world"),Opened.Store.IsValid()))return false;
    const auto Row=Opened.Store->Read({EAetherAggregateKind::World,TEXT("Main")}).Get();FAetherWorldStateV10 Reloaded;
    if(TestTrue(TEXT("Decode durable world after restart"),Row.Value.IsSet()&&AetherWorldCodec::Decode(Row.Value->Payload,D.Items,D.Rules,{},Reloaded,Why)))
    {
        TestEqual(TEXT("Checkpoint advances persistent revision"),Reloaded.Revision,int64(1));
        if(TestEqual(TEXT("Body preserved"),Reloaded.Bodies.Num(),1))
        {
            TestTrue(TEXT("Captured transform survives reopen"),Reloaded.Bodies[0].Transform.GetLocation().Equals(SavedPosition));
            TestEqual(TEXT("Captured water survives reopen"),Reloaded.Bodies[0].WaterKg,.5);
        }
    }
    Opened.Store->Close();return true;
}
#endif
