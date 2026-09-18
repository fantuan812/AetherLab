#include "Misc/AutomationTest.h"
#include "Persistence/AetherLegacyV9Reader.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherV10LegacyReaderTest,
    "Aether.V10.Migration.ExplicitLegacyReader",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAetherV10LegacyReaderTest::RunTest(const FString&)
{
    TestEqual(TEXT("v9 reflection graph remains frozen"), AetherLegacyV9::LayoutFingerprint(), FString(TEXT("c95976b1682573e91fd1428684ee763e")));
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Read frozen v9 fixture"), FFileHelper::LoadFileToArray(Bytes,
        *(FPaths::ProjectDir()/TEXT("Docs/Fixtures/V9/Profiles.sav"))))) return false;
    const auto Original = Bytes;
    auto Read = AetherLegacyV9::Read(Bytes);
    if (!TestTrue(*Read.Detail, Read.Code == EAetherLegacyReadCode::Ready)) return false;
    TestTrue(TEXT("Reader leaves source bytes untouched"), Bytes == Original);
    const auto& Save = *Read.Snapshot;
    TestEqual(TEXT("All historical spell combinations and edge cases"), Save.Profiles.Num(), 19);
    for (int32 I = 0; I < 16; ++I)
        TestEqual(TEXT("Old spell mask retained"), Save.Profiles[I].LearnedSpells, uint8(I));
    TestEqual(TEXT("Full bag retains all instances"), Save.Profiles[16].Inventory.Num(), 32);
    TestEqual(TEXT("Stable instance identity"), Save.Profiles[16].Inventory[31].InstanceId, FGuid(0xA37E0010,17,32,9));
    TestEqual(TEXT("Two-handed equipment stays a reference"), Save.Profiles[17].Equipped.Num(), 1);
    TestEqual(TEXT("Unclaimed reward retained"), Save.Profiles[18].PendingGold, 37);

    auto Reject = [&](const TArray<uint8>& Input, const TCHAR* Label)
    {
        auto Bad = AetherLegacyV9::Read(Input);
        TestTrue(Label, Bad.Code != EAetherLegacyReadCode::Ready && !Bad.Snapshot.IsValid() && !Bad.Detail.IsEmpty());
    };
    // 此偏移来自提交中固定 SHA256 的夹具；先验证计数，再模拟磁盘损坏/恶意超大数量。
    constexpr int32 CountOffset = 2133;
    int32 Count = 0; FMemory::Memcpy(&Count, Bytes.GetData()+CountOffset, sizeof(Count));
    TestEqual(TEXT("Frozen array offset still addresses profile count"), Count, 19);
    for (int32 CorruptCount : {MAX_int32, MIN_int32, -1, 129, 0})
    {
        Bytes = Original; FMemory::Memcpy(Bytes.GetData()+CountOffset, &CorruptCount, sizeof(CorruptCount));
        Reject(Bytes, TEXT("Corrupt array count cannot allocate or skip payload"));
    }
    Bytes = Original; Bytes[1977] = 2; Reject(Bytes, TEXT("Unknown class extension rejected"));
    Bytes = Original;
    Bytes.SetNum(Bytes.Num()-1); Reject(Bytes, TEXT("Truncation fails closed"));
    Bytes = Original; Bytes.Add(0); Reject(Bytes, TEXT("Trailing bytes fail closed"));
    Bytes = Original; Bytes[0] = 0; Reject(Bytes, TEXT("Unknown container format rejected"));
    Bytes = Original; Bytes[4] = 99; Reject(Bytes, TEXT("Future GVAS header rejected"));
    Bytes = Original;
    // Header 的类名只参与白名单校验，绝不通过 LoadClass 创建任意文件指定类型。
    const auto Needle = StringCast<UTF8CHAR>(TEXT("AetherFrontierSave"));
    for (int32 I = 0; I <= Bytes.Num()-Needle.Length(); ++I)
        if (FMemory::Memcmp(Bytes.GetData()+I, Needle.Get(), Needle.Length()) == 0)
        { Bytes[I] = 'X'; break; }
    Reject(Bytes, TEXT("Unlisted class rejected"));

    auto* Candidate = DuplicateObject<UAetherFrontierSave>(Read.Snapshot.Get(), GetTransientPackage());
    auto RejectSnapshot = [&](const TCHAR* Label)
    {
        TArray<uint8> Encoded;
        TestTrue(TEXT("Synthetic corruption writer"), UGameplayStatics::SaveGameToMemory(Candidate, Encoded));
        Reject(Encoded, Label);
    };
    Candidate->Version = 999; RejectSnapshot(TEXT("Future application schema rejected"));
    Candidate->Version = 5;
    const auto Duplicate = Candidate->Profiles[0]; Candidate->Profiles.Add(Duplicate);
    RejectSnapshot(TEXT("Duplicate profile ownership rejected"));
    Candidate->Profiles.SetNum(129); RejectSnapshot(TEXT("Oversized array rejected before UE allocation"));

    // 冻结夹具没有世界体，补测 Guid/Transform、集合中的结构与 Map 标签格式。
    Candidate = NewObject<UAetherFrontierSave>();
    Candidate->Generation = 12;
    FAetherProfile P; P.CharacterId = TEXT("LegacyWorldOwner"); P.Revision = 2;
    P.Add(TEXT("Potion"), 2); Candidate->Profiles.Add(P);
    FReactiveSaveRecord W; W.RegionId = TEXT("Town"); W.StableId = TEXT("LegacyBox");
    W.Transform = FTransform(FRotator(1,2,3), FVector(100,200,300), FVector(2,2,2));
    W.bGateOpen = true; W.EnthalpyJ = 123.5; Candidate->World.Add(W);
    FAetherWorldLoot L; L.ClaimId = FGuid(1,2,3,4); L.Items.Add(TEXT("Material"), 2);
    L.ClaimedBy = P.CharacterId; Candidate->Loot.Add(L);
    FAetherInventoryReceipt Receipt;
    Receipt.Command.CommandId = FGuid(5,6,7,8); Receipt.Command.ExpectedInventoryRevision = 1;
    Receipt.Command.Action = TEXT("Buy"); Receipt.Command.DefinitionId = TEXT("Potion");
    Receipt.FinalRevision = 2; Receipt.Transferred = 1;
    Candidate->Profiles[0].InventoryReceipts.Add(Receipt);
    Bytes.Reset(); UGameplayStatics::SaveGameToMemory(Candidate, Bytes);
    auto WorldRead = AetherLegacyV9::Read(Bytes);
    if (TestTrue(*WorldRead.Detail, WorldRead.Code == EAetherLegacyReadCode::Ready))
    {
        TestTrue(TEXT("World transform preserved"), WorldRead.Snapshot->World[0].Transform.Equals(W.Transform));
        TestTrue(TEXT("Claimed loot preserved"), WorldRead.Snapshot->Loot[0].ClaimedBy == P.CharacterId);
        TestEqual(TEXT("Historical receipt retained"), WorldRead.Snapshot->Profiles[0].InventoryReceipts[0].Command.CommandId, Receipt.Command.CommandId);
    }
    Candidate->World[0].SourceAge = -1; RejectSnapshot(TEXT("Negative world source age rejected"));
    Candidate->World[0].SourceAge = 0; Candidate->World[0].ElectricalWetness01 = 2;
    RejectSnapshot(TEXT("Invalid electrical water state rejected"));
    Candidate->World[0].ElectricalWetness01 = 0;
    Candidate->World[0].Transform.SetScale3D(FVector::ZeroVector);
    RejectSnapshot(TEXT("Invalid world transform rejected"));
    return true;
}
#endif
