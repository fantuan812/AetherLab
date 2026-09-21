#include "Misc/AutomationTest.h"
#include "Framework/AetherFrontier.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
// 先用 v9 的真实反射布局冻结二进制，再开始模块/字段迁移。
// 此夹具只含合成角色，绝不读取或导出用户个人存档。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherV10FreezeLegacy,
    "Aether.V10.Baseline.LegacySaveFixtures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAetherV10FreezeLegacy::RunTest(const FString&)
{
    const FString FixturePath = FPaths::ProjectDir() / TEXT("Docs/Fixtures/V9/Profiles.sav");
    const bool Generate = FParse::Param(FCommandLine::Get(), TEXT("AetherFreezeV9"));
    if (Generate)
    {
        // 生成必须显式选择；普通回归只读提交中的夹具，不能用新布局覆盖旧证据。
        if (IFileManager::Get().FileExists(*FixturePath))
        {
            AddError(TEXT("Frozen v9 fixture already exists; refusing to overwrite."));
            return false;
        }
        auto* Save = NewObject<UAetherFrontierSave>();
        Save->Version = 5;
        Save->Generation = 9;
        for (int32 Mask = 0; Mask < 16; ++Mask)
        {
            FAetherProfile Profile;
            Profile.CharacterId = FString::Printf(TEXT("V9Spells%02d"), Mask);
            Profile.LearnedSpells = uint8(Mask);
            Save->Profiles.Add(Profile);
        }
        FAetherProfile Full;
        Full.CharacterId = TEXT("V9Full");
        for (int32 Slot = 0; Slot < FAetherRules::Get().InventoryCapacity; ++Slot)
            Full.Add("TrainingSword", 1);
        Save->Profiles.Add(Full);

        FAetherProfile TwoHanded;
        TwoHanded.CharacterId = TEXT("V9TwoHanded");
        TwoHanded.Add("TrainingHammer", 1);
        TestTrue(TEXT("Fixture hammer equips"), TwoHanded.Equip(TwoHanded.Inventory[0].InstanceId));
        Save->Profiles.Add(TwoHanded);

        FAetherProfile Pending;
        Pending.CharacterId = TEXT("V9Pending");
        Pending.PendingGold = 37;
        Pending.PendingMaterial = 3;
        Save->Profiles.Add(Pending);

        // GUID 固定便于定位迁移前后的同一件物品；合法装备引用也一起更新。
        for (int32 P = 0; P < Save->Profiles.Num(); ++P)
        {
            auto& Profile = Save->Profiles[P];
            for (int32 I = 0; I < Profile.Inventory.Num(); ++I)
            {
                const FGuid Old = Profile.Inventory[I].InstanceId;
                const FGuid Stable(0xA37E0010, P + 1, I + 1, 9);
                for (auto& Equipped : Profile.Equipped)
                    if (Equipped.Value == Old) Equipped.Value = Stable;
                Profile.Inventory[I].InstanceId = Stable;
            }
            TestTrue(TEXT("Synthetic v9 profile is legal"), Profile.Validate());
        }
        TArray<uint8> Bytes;
        if (!TestTrue(TEXT("v9 writer serializes"), UGameplayStatics::SaveGameToMemory(Save, Bytes))) return false;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(FixturePath), true);
        if (!TestTrue(TEXT("Write frozen fixture"), FFileHelper::SaveArrayToFile(Bytes, *FixturePath))) return false;
    }
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Frozen fixture exists"), FFileHelper::LoadFileToArray(Bytes, *FixturePath))) return false;
    auto* Save = Cast<UAetherFrontierSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("v9 save reader recognizes fixture"), Save)) return false;
    TestEqual(TEXT("All bitmasks plus full/twohand/pending"), Save->Profiles.Num(), 19);
    for (const auto& Profile : Save->Profiles) TestTrue(TEXT("Loaded profile invariant"), Profile.Validate());
    TestEqual(TEXT("Full bag is not truncated"), Save->Profiles[16].Inventory.Num(), 32);
    TestEqual(TEXT("Two-handed reference retained"), Save->Profiles[17].Equipped.Num(), 1);
    TestEqual(TEXT("Pending reward retained"), Save->Profiles[18].PendingGold, 37);
    return true;
}
#endif
