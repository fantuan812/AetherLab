#include "Misc/AutomationTest.h"
#include "Persistence/AetherLegacyV9Reader.h"
#include "Persistence/AetherLegacyProfileConverter.h"
#include "Profile/AetherProfileCodec.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherProfileConversionTest,"Aether.V10.Migration.FullLegacyProfileConversion",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherProfileConversionTest::RunTest(const FString&)
{
    const FString Digest=TEXT("9e2b795785ba0d4e8da95f5baf01425db56fcaf3ba861fc2a24dc5733621b693");
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);
    const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto& Rules=FAetherRules::Get();
    TArray<uint8> Fixture;FFileHelper::LoadFileToArray(Fixture,*(FPaths::ProjectDir()/TEXT("Docs/Fixtures/V9/Profiles.sav")));
    auto Read=AetherLegacyV9::Read(Fixture);
    if(!TestTrue(*Read.Detail,Read.Code==EAetherLegacyReadCode::Ready))return false;
    for(const auto& Old:Read.Snapshot->Profiles)
    {
        FAetherProfileStateV10 Converted,Reloaded;TArray<uint8> Bytes;
        if(!TestTrue(*Reason,AetherLegacyV9::ConvertProfile(Old,5,Digest,Items,Skills,Rules,Converted,Reason)))return false;
        TestEqual(TEXT("All legacy instances retained"),Converted.Inventory.Items.Num(),Old.Inventory.Num());
        for(int32 Index=0;Index<Old.Inventory.Num();++Index)
        {
            const auto* Item=Converted.Inventory.At(Index);
            TestTrue(TEXT("Original compact order becomes fixed cell, identity/count unchanged"),Item&&Item->InstanceId==Old.Inventory[Index].InstanceId&&Item->Quantity==Old.Inventory[Index].Count);
        }
        for(const auto& Slot:Old.Equipped)TestTrue(TEXT("Equipment retains original GUID"),Converted.Inventory.Equipment.FindRef(Slot.Key.ToString())==Slot.Value);
        for(int32 Bit=0;Bit<4;++Bit)TestEqual(TEXT("Historical masks become story bases"),Converted.Skills.PermanentRank(Skills.Legacy(Bit)->SkillId),(Old.LearnedSpells&(1<<Bit))?1:0);
        if(!TestTrue(TEXT("Every converted profile survives explicit DTO"),AetherProfileCodec::Encode(Converted,Items,Skills,Rules,Bytes,Reason)&&AetherProfileCodec::Decode(Bytes,Items,Skills,Rules,Reloaded,Reason)))return false;
    }
    const auto& Pending=Read.Snapshot->Profiles[18];FAetherProfileStateV10 A,B;
    AetherLegacyV9::ConvertProfile(Pending,5,Digest,Items,Skills,Rules,A,Reason);
    AetherLegacyV9::ConvertProfile(Pending,5,Digest,Items,Skills,Rules,B,Reason);
    if(!TestEqual(TEXT("Unique pending reward record"),A.PendingRewards.Num(),1))return false;
    TestTrue(TEXT("Repeated conversion yields same reward identity"),A.PendingRewards[0].RewardId==B.PendingRewards[0].RewardId);
    TestTrue(TEXT("Old pending quantities retained"),A.PendingRewards[0].Gold==37&&A.PendingRewards[0].Items.FindRef(TEXT("Material"))==3);
    auto Advanced=Pending;Advanced.Revision=9;Advanced.Gold=88;Advanced.Experience=777;Advanced.bRegistered=true;Advanced.bCompanion=true;
    Advanced.LastAbbeyReceipt=FGuid(1,2,3,4);Advanced.LastRelayReceipt=FGuid(2,3,4,5);
    Advanced.Evidence={TEXT("supplya"),TEXT("SUPPLYB"),TEXT("Gate")};Advanced.Claims={TEXT("q_main_01")};
    Advanced.DailyDate=TEXT("20260919");Advanced.DailyEvidence={TEXT("Patrol0")};Advanced.DailyClaims={TEXT("Daily0")};
    FAetherInventoryReceipt Receipt;Receipt.Command.CommandId=FGuid(1,7,8,9);Receipt.Command.ExpectedInventoryRevision=8;
    Receipt.Command.Action=TEXT("Buy");Receipt.Command.DefinitionId=TEXT("Potion");Receipt.Command.ShopId=TEXT("Apothecary");
    Receipt.FinalRevision=9;Receipt.Transferred=1;Advanced.InventoryReceipts.Add(Receipt);
    if(!TestTrue(*Reason,AetherLegacyV9::ConvertProfile(Advanced,5,Digest,Items,Skills,Rules,B,Reason)))return false;
    TestTrue(TEXT("Revision/flags/experience are preserved"),B.Revision==9&&B.LegacyProfileRevision==9&&B.Gold==88&&B.Experience==777&&B.bRegistered&&B.bCompanion);
    TestTrue(TEXT("Encounters and progression survive"),B.LastAbbeyReceipt==Advanced.LastAbbeyReceipt&&B.LastRelayReceipt==Advanced.LastRelayReceipt&&B.Claims.Contains(TEXT("Q_Main_01"))&&B.DailyClaims.Contains(TEXT("Daily0")));
    TestTrue(TEXT("Historical success cannot become an executable new request"),B.LegacyInventoryReceipts.Num()==1&&B.LegacyInventoryReceipts[0].CommandId==Receipt.Command.CommandId&&B.LegacyInventoryReceipts[0].ExpectedRevision==8);
    auto Smaller=Items;Smaller.DefaultCapacity=16;
    TestFalse(TEXT("Full old bag cannot be silently truncated"),AetherLegacyV9::ConvertProfile(Read.Snapshot->Profiles[16],5,Digest,Smaller,Skills,Rules,B,Reason));
    TestEqual(TEXT("Failed conversion leaves previous output intact"),B.Gold,88);
    auto Unknown=Advanced;FAetherItemStack UnknownItem;UnknownItem.InstanceId=FGuid(1,1,1,1);UnknownItem.DefinitionId=TEXT("MissingItem");UnknownItem.Count=1;Unknown.Inventory.Add(UnknownItem);
    TestFalse(TEXT("Unknown item requires diagnosis, not deletion"),AetherLegacyV9::ConvertProfile(Unknown,5,Digest,Items,Skills,Rules,B,Reason));
    TestEqual(TEXT("Reader snapshot was not mutated"),Read.Snapshot->Profiles[18].Revision,0);
    return true;
}
#endif
