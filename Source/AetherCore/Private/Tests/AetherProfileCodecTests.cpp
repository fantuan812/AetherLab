#include "Misc/AutomationTest.h"
#include "Profile/AetherProfileCodec.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherProfileCodecTest,"Aether.V10.Profile.FullExplicitDTOAndBounds",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherProfileCodecTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);
    const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto& Rules=FAetherRules::Get();
    FAetherProfileStateV10 S;S.CharacterId=TEXT("合成角色");S.Revision=7;S.Gold=123;S.Experience=456;
    S.bRegistered=true;S.bCompanion=true;S.LastAbbeyReceipt=FGuid(1,2,3,4);S.LastRelayReceipt=FGuid(2,3,4,5);
    FAetherV10ItemInstance Item;Item.InstanceId=FGuid(8,7,6,5);Item.DefinitionId=TEXT("TrainingSword");Item.SlotIndex=19;
    Item.Durability=Items.Items[Item.DefinitionId].MaxDurability>0?Items.Items[Item.DefinitionId].MaxDurability:-1;
    S.Inventory.Items.Add(Item);S.Inventory.Equip(Item.InstanceId,TEXT("MainHand"),S.CharacterId,Items);
    S.Skills.GrantStory(TEXT("Fire.Ignite"),TEXT("Story.Training"),Skills);
    S.Skills.AwardPoints(TEXT("Quest.Test"),3,Skills);S.Skills.Bind(0,TEXT("Fire.Ignite"),{},Skills);
    S.Evidence={TEXT("SupplyA"),TEXT("SupplyB"),TEXT("Gate")};S.Claims={TEXT("Q_Main_01")};
    S.DailyDate=TEXT("20260919");S.DailyEvidence={TEXT("Patrol0")};S.DailyClaims={TEXT("Daily0")};
    FAetherPendingRewardV10 Reward;Reward.RewardId=FGuid(4,3,2,1);Reward.SourceId=TEXT("Quest.Pending");Reward.Gold=37;Reward.Items.Add(TEXT("Material"),3);
    S.PendingRewards.Add(Reward);S.ClaimedRewardIds.Add(FGuid(7,7,7,7));
    S.LegacySaveSchema=5;S.LegacyProfileRevision=7;S.LegacySourceSha256=FString::ChrN(64,'a');
    FAetherLegacyInventoryReceiptV9 Receipt;Receipt.CommandId=FGuid(9,8,7,6);Receipt.ItemInstanceId=Item.InstanceId;
    Receipt.ExpectedRevision=6;Receipt.FinalRevision=7;Receipt.Quantity=1;Receipt.Transferred=1;Receipt.Action=TEXT("Equip");
    S.LegacyInventoryReceipts.Add(Receipt);
    TArray<uint8> Bytes,Again;FAetherProfileStateV10 Out;
    if(!TestTrue(*Reason,AetherProfileCodec::Encode(S,Items,Skills,Rules,Bytes,Reason)))return false;
    if(!TestTrue(*Reason,AetherProfileCodec::Decode(Bytes,Items,Skills,Rules,Out,Reason)))return false;
    TestEqual(TEXT("Unicode identity retained"),Out.CharacterId,S.CharacterId);
    TestEqual(TEXT("Original profile version retained"),Out.Revision,int64(7));
    TestTrue(TEXT("Economy and flags retained"),Out.Gold==123&&Out.Experience==456&&Out.bRegistered&&Out.bCompanion);
    TestTrue(TEXT("Inventory/equipment DTO nested losslessly"),Out.Inventory.At(19)&&Out.Inventory.Equipment.FindRef(TEXT("MainHand"))==Item.InstanceId);
    TestEqual(TEXT("Skill source and points retained"),Out.Skills.AvailableSkillPoints,3);
    TestTrue(TEXT("Quest/daily progression retained"),Out.Claims==S.Claims&&Out.Evidence==S.Evidence&&Out.DailyClaims==S.DailyClaims&&Out.DailyEvidence==S.DailyEvidence&&Out.DailyDate==S.DailyDate);
    TestTrue(TEXT("Pending reward and claim ledger retained"),Out.PendingRewards.Num()==1&&Out.PendingRewards[0].Gold==37&&Out.PendingRewards[0].Items.FindRef(TEXT("Material"))==3&&Out.ClaimedRewardIds.Contains(FGuid(7,7,7,7)));
    TestTrue(TEXT("Old receipt remains archival, including command identity"),Out.LegacyInventoryReceipts.Num()==1&&Out.LegacyInventoryReceipts[0].CommandId==Receipt.CommandId&&Out.LegacyInventoryReceipts[0].FinalRevision==7&&Out.LegacySourceSha256==S.LegacySourceSha256);
    TestTrue(TEXT("Complete encoding roundtrips canonically"),AetherProfileCodec::Encode(Out,Items,Skills,Rules,Again,Reason)&&Bytes==Again);
    for(int32 N=0;N<Bytes.Num();++N)
    {
        TArray<uint8> Cut;Cut.Append(Bytes.GetData(),N);
        // WER1 之前的完整历史 DTO 仍可读；仅这一个精确边界不是截断错误。
        if(N==Bytes.Num()-12)TestTrue(TEXT("Complete pre-WER1 DTO remains compatible"),AetherProfileCodec::Decode(Cut,Items,Skills,Rules,Out,Reason)&&Out.WearSequence==0);
        else TestFalse(TEXT("Partial base or extension rejected"),AetherProfileCodec::Decode(Cut,Items,Skills,Rules,Out,Reason));
        TestEqual(TEXT("Failure never clears caller profile"),Out.Gold,123);
    }
    Again=Bytes;Again[4]=99;TestFalse(TEXT("Unknown profile schema rejected"),AetherProfileCodec::Decode(Again,Items,Skills,Rules,Out,Reason));
    Again=Bytes;Again.Add(0);TestFalse(TEXT("Trailing bytes rejected"),AetherProfileCodec::Decode(Again,Items,Skills,Rules,Out,Reason));
    const int32 NameBytes=int32(Bytes[8])+(int32(Bytes[9])<<8);
    const int32 InventoryLengthOffset=10+NameBytes+8+4+4+1+32;
    Again=Bytes;for(int32 I=0;I<4;++I)Again[InventoryLengthOffset+I]=255;
    TestFalse(TEXT("Nested blob length cannot allocate unbounded memory"),AetherProfileCodec::Decode(Again,Items,Skills,Rules,Out,Reason));
    auto Bad=S;Bad.DailyDate=TEXT("20260230");TestFalse(TEXT("Impossible date rejected"),Bad.Validate(Items,Skills,Rules,Reason));
    Bad=S;Bad.Claims.Add(TEXT("Unknown.Quest"));TestFalse(TEXT("Unknown quest rejected without deleting it"),Bad.Validate(Items,Skills,Rules,Reason));
    Bad=S;Bad.ClaimedRewardIds.Add(Reward.RewardId);TestFalse(TEXT("Reward cannot be both pending and claimed"),Bad.Validate(Items,Skills,Rules,Reason));
    Bad=S;Bad.Revision=0;TestFalse(TEXT("Migration cannot reset below old version"),Bad.Validate(Items,Skills,Rules,Reason));
    return true;
}
#endif
