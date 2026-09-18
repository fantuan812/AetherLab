#include "Misc/AutomationTest.h"
#include "Commands/AetherProfileCommand.h"
#include "Commands/AetherProfileCoordinator.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Profile/AetherProfileCodec.h"
#include "Persistence/AetherSqliteStore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FAetherPlayerCommand Make(EAetherCommandType Type,int64 Revision)
{FAetherPlayerCommand C;C.Type=Type;C.ExpectedProfileRevision=Revision;C.CommandId=AetherTransactions::NewCommandId(Revision);return C;}
FAetherProfileCommandContext ShopContext(const FString& Shop)
{
    FAetherProfileCommandContext C;C.bCanManageInventory=true;C.bTradeSessionValid=true;C.ShopId=Shop;C.TradeTargetStableId=TEXT("Merchant.Test");return C;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherEconomyRulesTest,"Aether.V10.Economy.DefinitionsCapacityAndAuthority",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherEconomyRulesTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);
    FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Economy.json")));
    const auto Economy=FAetherEconomyDefinitionsV10::Parse(Json,Items,Reason);
    if(!TestTrue(*Reason,Economy.Validate(Items,Reason)))return false;
    auto Bad=Economy;Bad.Shops[TEXT("Shop")].Products.Add(TEXT("Missing"));TestFalse(TEXT("Unknown shop product rejected"),Bad.Validate(Items,Reason));
    Bad=Economy;Bad.Shops[TEXT("Shop")].Products.Add(TEXT("Potion"));TestFalse(TEXT("Duplicate product rejected"),Bad.Validate(Items,Reason));
    Bad=Economy;Bad.Shops[TEXT("Armorer")].RepairGoldPerPoint=0;TestFalse(TEXT("Repair must define positive authoritative cost"),Bad.Validate(Items,Reason));
    const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto& Rules=FAetherRules::Get();
    FAetherProfileStateV10 P;P.CharacterId=TEXT("Alice");P.Gold=1000;
    FAetherV10ItemInstance I;I.InstanceId=FGuid(1,2,3,4);I.DefinitionId=TEXT("Potion");I.Quantity=2;I.SlotIndex=0;I.Quality=1;I.bLocked=true;P.Inventory.Items.Add(I);
    auto C=Make(EAetherCommandType::BuyItem,0);C.TargetStableId=TEXT("Merchant.Test");C.DefinitionId=TEXT("Potion");C.Quantity=3;
    auto Context=ShopContext(TEXT("Shop"));FAetherTransaction T;FAetherCommandResult Result;
    const auto Prepare=[&](){return AetherProfileCommands::Prepare(C,TEXT("Alice"),P,Context,Items,Skills,Rules,T,Result,Economy);};
    if(!TestTrue(TEXT("Buy catalog product"),Prepare()))return false;
    FAetherProfileStateV10 Next;AetherProfileCodec::Decode(T.Writes[0].Value.Payload,Items,Skills,Rules,Next,Reason);
    TestTrue(TEXT("Purchase never washes quality/lock into existing stack"),Next.Inventory.Find(I.InstanceId)->Quantity==2&&Next.Inventory.At(1)->Quantity==3&&Next.Inventory.At(1)->Quality==0);
    TestEqual(TEXT("Buy price comes from item definition"),Next.Gold,1000-3*Items.Items[TEXT("Potion")].BuyPrice);
    C.Quantity=1000;TestFalse(TEXT("Authoritative total cost rejects insufficient currency"),Prepare());TestTrue(TEXT("Cost failure is explicit"),Result.Code==EAetherCommandCode::InsufficientFunds);C.Quantity=3;
    Context.bTradeSessionValid=false;TestFalse(TEXT("Closed or out-of-range service cannot trade"),Prepare());TestTrue(TEXT("Explicit invalid session result"),Result.Code==EAetherCommandCode::OutOfReach);
    Context=ShopContext(TEXT("Shop"));C.TargetStableId=TEXT("AnotherMerchant");TestFalse(TEXT("Cannot reuse another merchant session"),Prepare());
    C.TargetStableId=TEXT("Merchant.Test");Context=ShopContext(TEXT("Armorer"));TestFalse(TEXT("Merchant does not expose union of all catalogs"),Prepare());
    Context=ShopContext(TEXT("Shop"));C=Make(EAetherCommandType::SellItem,0);C.TargetStableId=TEXT("Merchant.Test");C.ItemInstanceId=I.InstanceId;C.Quantity=1;
    TestFalse(TEXT("Locked item cannot be sold"),Prepare());P.Inventory.Items[0].bLocked=false;P.Inventory.Items[0].BoundToCharacter=TEXT("Alice");TestFalse(TEXT("Binding blocks sale even for owner"),Prepare());
    P.Inventory.Items[0].BoundToCharacter.Reset();if(!TestTrue(TEXT("Explicit unlocked unbound sale allowed"),Prepare()))return false;
    AetherProfileCodec::Decode(T.Writes[0].Value.Payload,Items,Skills,Rules,Next,Reason);
    TestTrue(TEXT("Partial sale keeps original instance metadata"),Next.Inventory.Find(I.InstanceId)->Quantity==1&&Next.Inventory.Find(I.InstanceId)->Quality==1&&Next.Gold==1000+Items.Items[TEXT("Potion")].SellPrice);
    // 满包先有可合并空间，后一个奖励品类无格子：整条领取必须失败，不留下先发的一半。
    P.Inventory.Items.Reset();
    for(int32 N=0;N<32;++N){auto F=I;F.InstanceId=FGuid(9,0,0,N+1);F.Quality=0;F.bLocked=false;F.SlotIndex=N;F.Quantity=N==0?19:20;P.Inventory.Items.Add(F);}
    FAetherPendingRewardV10 Reward;Reward.RewardId=FGuid(8,7,6,5);Reward.SourceId=TEXT("Quest.Test");Reward.Gold=37;Reward.Items={{TEXT("Potion"),1},{TEXT("Ration"),1}};P.PendingRewards.Add(Reward);
    C=Make(EAetherCommandType::ClaimReward,0);C.DefinitionId=TEXT("Reward.")+Reward.RewardId.ToString(EGuidFormats::Digits);
    T.ActorId=TEXT("Sentinel");TestFalse(TEXT("Partial-fit reward rejected atomically"),Prepare());
    TestTrue(TEXT("Full bag retains all pending value and currency"),Result.Code==EAetherCommandCode::Capacity&&Result.ActualQuantity==0&&Result.AffectedIds.IsEmpty()&&P.Gold==1000&&P.PendingRewards.Num()==1&&P.Inventory.At(0)->Quantity==19&&P.ClaimedRewardIds.IsEmpty()&&T.ActorId==TEXT("Sentinel"));
    TArray<uint8> FailureReply;TestTrue(TEXT("Partial-fit rejection is a valid non-transfer reply"),AetherCommands::EncodeResult(Result,FailureReply,Reason));
    P.Inventory.Items.RemoveAt(31);TestTrue(TEXT("Freeing one slot allows entire reward"),Prepare());
    AetherProfileCodec::Decode(T.Writes[0].Value.Payload,Items,Skills,Rules,Next,Reason);
    TestTrue(TEXT("Full reward and claim ledger share candidate"),Next.Gold==1037&&Next.PendingRewards.IsEmpty()&&Next.ClaimedRewardIds.Contains(Reward.RewardId)&&Next.Inventory.At(0)->Quantity==20&&Next.Inventory.At(31)->DefinitionId==TEXT("Ration"));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherEconomyTransactionTest,"Aether.V10.Economy.DurableBuyRepairAndReward",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherEconomyTransactionTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);
    FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Economy.json")));
    const auto Economy=FAetherEconomyDefinitionsV10::Parse(Json,Items,Reason);const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto& Rules=FAetherRules::Get();
    if(!TestTrue(*Reason,Economy.Validate(Items,Reason)))return false;
    FAetherProfileStateV10 P;P.CharacterId=TEXT("Alice");P.Gold=1000;
    FString Gear;for(const auto& V:Items.Items)if(V.Value.MaxDurability>=10){Gear=V.Key;break;}
    if(!TestFalse(TEXT("Production catalog has repairable equipment"),Gear.IsEmpty()))return false;
    FAetherV10ItemInstance I;I.InstanceId=FGuid(1,8,3,4);I.DefinitionId=Gear;I.SlotIndex=0;I.Durability=Items.Items[Gear].MaxDurability-10;P.Inventory.Items.Add(I);
    FAetherPendingRewardV10 Reward;Reward.RewardId=FGuid(7,6,5,4);Reward.SourceId=TEXT("Quest.Test");Reward.Gold=37;Reward.Items.Add(TEXT("Potion"),2);P.PendingRewards.Add(Reward);
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10Economy")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    O.Fault=MakeShared<TAtomic<EAetherStoreFault>,ESPMode::ThreadSafe>(EAetherStoreFault::None);
    auto DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Open isolated economy store"),DB.Store.IsValid()))return false;
    FAetherTransaction Seed;Seed.ActorId=P.CharacterId;Seed.ExpectedProfileRevision=-1;Seed.CommandId=AetherTransactions::NewCommandId(-1);Seed.Request={1};
    FAetherAggregateWrite Row;Row.Value.Key={EAetherAggregateKind::Profile,P.CharacterId};
    AetherProfileCodec::Encode(P,Items,Skills,Rules,Row.Value.Payload,Reason);Seed.Writes.Add(Row);
    TestTrue(TEXT("Seed existing equipment and pending reward"),DB.Store->Commit(Seed).Get().Code==EAetherStoreCode::Committed);
    const auto Reload=[&]()
    {
        const auto Read=DB.Store->Read(Row.Value.Key).Get();
        return Read.Value.IsSet()&&AetherProfileCodec::Decode(Read.Value->Payload,Items,Skills,Rules,P,Reason)&&Read.Value->Revision==P.Revision;
    };
    auto Context=ShopContext(TEXT("Armorer"));auto C=Make(EAetherCommandType::RepairItem,0);C.TargetStableId=TEXT("Merchant.Test");C.ItemInstanceId=I.InstanceId;
    FAetherTransaction T;FAetherCommandResult Result;
    const auto Prepare=[&](){return AetherProfileCommands::Prepare(C,P.CharacterId,P,Context,Items,Skills,Rules,T,Result,Economy);};
    if(!TestTrue(TEXT("Repair candidate includes cost"),Prepare())){DB.Store->Close();return false;}
    O.Fault->Store(EAetherStoreFault::AfterFirstWrite);TestTrue(TEXT("Failed repair transaction"),DB.Store->Commit(T).Get().Code==EAetherStoreCode::Unavailable);
    TestTrue(TEXT("Durability and gold both roll back"),Reload()&&P.Gold==1000&&P.Inventory.Find(I.InstanceId)->Durability==I.Durability);
    TestTrue(TEXT("Repair retry commits"),DB.Store->Commit(T).Get().Code==EAetherStoreCode::Committed);
    TestTrue(TEXT("Durability and cost commit together"),Reload()&&P.Gold==990&&P.Inventory.Find(I.InstanceId)->Durability==Items.Items[Gear].MaxDurability);
    TestTrue(TEXT("Repair replay cannot charge twice"),DB.Store->Commit(T).Get().Code==EAetherStoreCode::Replayed&&Reload()&&P.Gold==990);
    Context=ShopContext(TEXT("Shop"));C=Make(EAetherCommandType::BuyItem,1);C.TargetStableId=TEXT("Merchant.Test");C.DefinitionId=TEXT("Potion");C.Quantity=3;
    {
        FAetherProfileCoordinator Coordinator(DB.Store.ToSharedRef(),Items,Skills,Rules,Economy);
        const auto Session=Coordinator.BeginSession(P.CharacterId);FAetherCommandResult Rejection;
        TestTrue(TEXT("Purchase accepted by asynchronous coordinator"),Coordinator.Submit(Session,C,Rejection));
        TArray<FAetherProfileCompletion> Completed;const double Deadline=FPlatformTime::Seconds()+5;
        const FAetherResolveProfileContext Resolve=[&](const auto&,const auto&,const auto&,auto& Out){Out=Context;return true;};
        while(Completed.IsEmpty()&&FPlatformTime::Seconds()<Deadline){Completed=Coordinator.Poll(Resolve);FPlatformProcess::Sleep(.001f);}
        if(!TestTrue(TEXT("Coordinator forwards authoritative economy catalog and publishes committed purchase"),
            Completed.Num()==1&&Completed[0].Result.Code==EAetherCommandCode::Applied&&Completed[0].bMayPublish&&Completed[0].Snapshot.IsSet()))
        {DB.Store->Close();return false;}
    }
    TestTrue(TEXT("Purchase stores gold and item together"),Reload()&&P.Gold==930&&P.Inventory.At(1)->Quantity==3);
    C=Make(EAetherCommandType::ClaimReward,2);C.DefinitionId=TEXT("Reward.")+Reward.RewardId.ToString(EGuidFormats::Digits);
    TestTrue(TEXT("Prepare whole pending reward"),Prepare());O.Fault->Store(EAetherStoreFault::AfterCommitBeforeReply);
    TestTrue(TEXT("Reward response lost after commit"),DB.Store->Commit(T).Get().Code==EAetherStoreCode::Unavailable);
    DB.Store->Close();DB.Store.Reset();DB=AetherSQLite::Open(O);
    if(!TestTrue(TEXT("Reopen economy store"),DB.Store.IsValid()))return false;
    TestTrue(TEXT("Recover original reward receipt"),DB.Store->Commit(T).Get().Code==EAetherStoreCode::Replayed);
    TestTrue(TEXT("Reward ledger and assets survive without duplication"),Reload()&&P.Revision==3&&P.Gold==967&&P.Inventory.At(1)->Quantity==5&&P.PendingRewards.IsEmpty()&&P.ClaimedRewardIds.Contains(Reward.RewardId));
    C=Make(EAetherCommandType::ClaimReward,3);C.DefinitionId=TEXT("Reward.")+Reward.RewardId.ToString(EGuidFormats::Digits);
    TestFalse(TEXT("New command ID cannot claim settled reward again"),Prepare());DB.Store->Close();return true;
}
#endif
