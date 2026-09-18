#include "Misc/AutomationTest.h"
#include "Commands/AetherProfileCoordinator.h"
#include "Persistence/AetherSqliteStore.h"
#include "Profile/AetherProfileCodec.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherProtocolReceiptTest,"Aether.V10.Commands.DurableV2ReceiptAndCrossVersionConflict",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherProtocolReceiptTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto& Rules=FAetherRules::Get();
    FAetherProfileStateV10 P;P.CharacterId=TEXT("Alice");FAetherV10ItemInstance Item;Item.InstanceId=FGuid::NewGuid();Item.DefinitionId=TEXT("Potion");Item.SlotIndex=0;P.Inventory.Items.Add(Item);
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10Protocol")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    auto DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Open protocol compatibility store"),DB.Store.IsValid()))return false;
    FAetherTransaction Seed;Seed.ActorId=P.CharacterId;Seed.ExpectedProfileRevision=-1;Seed.CommandId=AetherTransactions::NewCommandId(-1);Seed.Request={1};
    FAetherAggregateWrite Row;Row.Value.Key={EAetherAggregateKind::Profile,P.CharacterId};AetherProfileCodec::Encode(P,Items,Skills,Rules,Row.Value.Payload,Reason);Seed.Writes.Add(Row);
    TestTrue(TEXT("Legacy protocol transaction still seeds profile"),DB.Store->Commit(Seed).Get().Code==EAetherStoreCode::Committed);
    FAetherPlayerCommand C;C.ProtocolVersion=2;C.Type=EAetherCommandType::SetItemFavorite;C.CommandId=AetherTransactions::NewCommandId(0);
    C.ItemInstanceId=Item.InstanceId;C.Enabled=true;
    FAetherProfileCommandContext Context;Context.bCanManageInventory=true;FAetherTransaction T;FAetherCommandResult Result;
    TestTrue(TEXT("Existing domain handler accepts validated v2 request"),AetherProfileCommands::Prepare(C,P.CharacterId,P,Context,Items,Skills,Rules,T,Result));
    TestTrue(TEXT("Transaction retains request version"),T.ProtocolVersion==2);
    TestTrue(TEXT("Commit v2 request"),DB.Store->Commit(T).Get().Code==EAetherStoreCode::Committed);
    DB.Store->Close();DB.Store.Reset();DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Reopen receipt store"),DB.Store.IsValid()))return false;
    TestTrue(TEXT("Stored v2 receipt survives reopening"),DB.Store->LookupReceipt({P.CharacterId,C.CommandId,2,T.Request}).Get().Code==EAetherStoreCode::Replayed);
    auto Old=C;Old.ProtocolVersion=1;TArray<uint8> V1;AetherCommands::Encode(Old,V1,Reason);
    TestTrue(TEXT("Same command ID cannot be reinterpreted under v1"),DB.Store->LookupReceipt({P.CharacterId,C.CommandId,1,V1}).Get().Code==EAetherStoreCode::Conflict);
    TestTrue(TEXT("Protocol alone is part of receipt identity"),DB.Store->LookupReceipt({P.CharacterId,C.CommandId,1,T.Request}).Get().Code==EAetherStoreCode::Conflict);
    FAetherProfileCoordinator Coordinator(DB.Store.ToSharedRef(),Items,Skills,Rules);const auto Session=Coordinator.BeginSession(P.CharacterId);FAetherCommandResult Reject;
    TestTrue(TEXT("Coordinator accepts v2 retry"),Coordinator.Submit(Session,C,Reject));
    int32 Evaluated=0;const FAetherResolveProfileContext Resolve=[&](const auto&,const auto&,const auto&,auto& Out){++Evaluated;Out=Context;return true;};
    TArray<FAetherProfileCompletion> Done;const double Deadline=FPlatformTime::Seconds()+5;
    while(Done.IsEmpty()&&FPlatformTime::Seconds()<Deadline){Done=Coordinator.Poll(Resolve);FPlatformProcess::Sleep(.001f);}
    TestTrue(TEXT("Receipt precedes reevaluation even across request versions"),Done.Num()==1&&Done[0].Result.Code==EAetherCommandCode::Replayed&&Evaluated==0&&Done[0].Snapshot.IsSet()&&Done[0].Snapshot->Revision==1&&Done[0].Snapshot->Inventory.Items[0].bFavorite);
    DB.Store->Close();return true;
}
#endif
