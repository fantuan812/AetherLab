#include "Migration/AetherLegacyAuditCommandlet.h"
#include "Persistence/AetherLegacyV9Reader.h"
#include "Persistence/AetherLegacyProfileConverter.h"
#include "Profile/AetherProfileCodec.h"
#include "Persistence/AetherLegacyWorldConverter.h"
#include "Persistence/AetherSqliteStore.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Crc.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

UAetherLegacyAuditCommandlet::UAetherLegacyAuditCommandlet()
{
    IsClient = false; IsServer = false; IsEditor = true;
    LogToConsole = true; ShowErrorCount = true;
}

int32 UAetherLegacyAuditCommandlet::Main(const FString& Params)
{
    FString Source, Report;
    if (!FParse::Value(*Params, TEXT("Source="), Source) || !FParse::Value(*Params, TEXT("Report="), Report)) return 2;
    Source = FPaths::ConvertRelativePathToFull(Source);
    Report = FPaths::ConvertRelativePathToFull(Report);
    const FString ReportRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("V10Migration"));
    // 报告仅可写 Saved/V10Migration 下的新文件；参数错误不能覆盖原存档或工程资源。
    if (!FPaths::IsUnderDirectory(Report, ReportRoot) || IFileManager::Get().FileExists(*Report)) return 2;
    const int64 FileSize = IFileManager::Get().FileSize(*Source);
    if (FileSize < 64 || FileSize > 16*1024*1024) return 2;
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Source)) return 2;
    FSHA256Signature Signature;
    // GenericPlatform 的 SHA256 在 Windows 没有实现；使用引擎自带 OpenSSL。
    if (!SHA256(Bytes.GetData(), Bytes.Num(), Signature.Signature)) return 2;
    auto Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("phase"), TEXT("legacy_complete_snapshot_conversion"));
    Root->SetStringField(TEXT("sourceSha256"), Signature.ToString().ToLower());
    Root->SetStringField(TEXT("layoutFingerprint"), AetherLegacyV9::LayoutFingerprint());
    Root->SetBoolField(TEXT("databaseWritten"), false);
    Root->SetBoolField(TEXT("finalSchemaConverted"), false);
    auto Result = AetherLegacyV9::Read(Bytes);
    bool Valid = Result.Code == EAetherLegacyReadCode::Ready;
    bool ProfilesConverted = false;
    FString ConversionDetail,SnapshotDetail,ImportDetail;
    bool SnapshotConverted=false,DatabaseWritten=false,ImportVerified=false;
    const bool ImportRequested=FParse::Param(*Params,TEXT("Import"));
    Root->SetBoolField(TEXT("importRequested"),ImportRequested);
    // 导入器只创建本次备份目录下的新库，不接受覆盖生产库的路径参数。
    const FString DatabasePath=FPaths::GetPath(Report)/TEXT("state.sqlite");
    if(ImportRequested&&(!FPaths::IsUnderDirectory(Source,ReportRoot)||FPaths::GetPath(Source)!=FPaths::GetPath(Report)||
        IFileManager::Get().FileExists(*DatabasePath)))return 2;
    if (Valid)
    {
        const auto& Save = *Result.Snapshot;
        const bool Fixture = FParse::Param(*Params, TEXT("Fixture"));
        if (Fixture)
        {
            // 夹具没有生产提交侧车；不能让此开关绕过任意真实档案的校验。
            Valid = Signature.ToString().Equals(TEXT("9e2b795785ba0d4e8da95f5baf01425db56fcaf3ba861fc2a24dc5733621b693"), ESearchCase::IgnoreCase);
            if (!Valid) Result.Detail = TEXT("Fixture bypass only accepts the frozen synthetic fixture");
        }
        else if (Save.Version == 5)
        {
            FString Checksum;
            const FString Expected = FString::Printf(TEXT("%d:%u"), Save.Generation, FCrc::MemCrc32(Bytes.GetData(), Bytes.Num()));
            Valid = FFileHelper::LoadFileToString(Checksum, *FPaths::ChangeExtension(Source, TEXT("crc"))) && Checksum == Expected;
            if (!Valid) Result.Detail = TEXT("Missing or mismatched committed-generation checksum");
        }
        Root->SetNumberField(TEXT("sourceSchema"), Save.Version);
        Root->SetNumberField(TEXT("generation"), Save.Generation);
        Root->SetNumberField(TEXT("worldRecords"), Save.World.Num());
        Root->SetNumberField(TEXT("lootRecords"), Save.Loot.Num());
        Root->SetNumberField(TEXT("serviceReceipts"), Save.ServiceReceipts.Num());
        FString ItemJson,DefinitionReason;
        FFileHelper::LoadFileToString(ItemJson,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
        const auto Items=FAetherV10ItemDefinitions::Parse(ItemJson,DefinitionReason);
        ProfilesConverted=Valid&&Items.Validate(DefinitionReason);
        if(Valid&&!ProfilesConverted)ConversionDetail=DefinitionReason;
        TArray<TSharedPtr<FJsonValue>> Profiles;
        for (const auto& Profile : Save.Profiles)
        {
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("characterId"), Profile.CharacterId);
            Row->SetNumberField(TEXT("revision"), Profile.Revision);
            Row->SetNumberField(TEXT("instances"), Profile.Inventory.Num());
            Row->SetNumberField(TEXT("equipmentReferences"), Profile.Equipped.Num());
            Row->SetNumberField(TEXT("oldSpellMask"), Profile.LearnedSpells);
            Row->SetNumberField(TEXT("pendingGold"), Profile.PendingGold);
            Row->SetNumberField(TEXT("pendingMaterial"), Profile.PendingMaterial);
            // 校验成功才尝试只读转换；损坏侧车的旧档不能进入新 schema 路径。
            FAetherProfileStateV10 Converted;TArray<uint8> Payload;FString Detail;
            const bool ConvertedOK=Valid&&AetherLegacyV9::ConvertProfile(Profile,Save.Version,Signature.ToString().ToLower(),
                Items,FAetherSkillDefinitionsV10::Get(),FAetherRules::Get(),Converted,Detail)
                &&AetherProfileCodec::Encode(Converted,Items,FAetherSkillDefinitionsV10::Get(),FAetherRules::Get(),Payload,Detail);
            ProfilesConverted&=ConvertedOK;
            Row->SetBoolField(TEXT("converted"),ConvertedOK);
            if(ConvertedOK)
            {
                Row->SetNumberField(TEXT("dtoBytes"),Payload.Num());
                Row->SetNumberField(TEXT("fixedCapacity"),Converted.Inventory.Capacity);
                Row->SetNumberField(TEXT("storySkills"),Converted.Skills.StoryGrants.Num());
                Row->SetNumberField(TEXT("pendingRewardRecords"),Converted.PendingRewards.Num());
                Row->SetNumberField(TEXT("newProfileRevision"),Converted.Revision);
            }
            else if(Valid){Row->SetStringField(TEXT("conversionDetail"),Detail);ConversionDetail=Detail;}
            Profiles.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetArrayField(TEXT("profiles"), Profiles);
        FAetherLegacyImport Import;
        SnapshotConverted=ProfilesConverted&&AetherLegacyV9::ConvertSnapshot(Save,Signature.ToString().ToLower(),
            Items,FAetherSkillDefinitionsV10::Get(),FAetherRules::Get(),Import,SnapshotDetail);
        if(SnapshotConverted)
        {
            const auto& World=Import.Values.Last();
            Root->SetNumberField(TEXT("worldDtoBytes"),World.Payload.Num());
            Root->SetNumberField(TEXT("newWorldRevision"),World.Revision);
            Root->SetNumberField(TEXT("convertedAggregates"),Import.Values.Num());
        }
        if(ImportRequested&&SnapshotConverted)
        {
            FAetherSqliteOptions Options;Options.DatabasePath=DatabasePath;
            auto DB=AetherSQLite::Open(Options);
            if(DB.Store)
            {
                const auto Committed=DB.Store->ImportLegacy(Import).Get();
                DatabaseWritten=Committed.Code==EAetherStoreCode::Committed;
                ImportDetail=Committed.Detail;
                DB.Store->Close();DB.Store.Reset();
                // 关闭并重新打开，复核全部行的版本和负载；不能只凭一次提交返回值宣布可采用。
                if(DatabaseWritten)
                {
                    DB=AetherSQLite::Open(Options);ImportVerified=DB.Store.IsValid();
                    if(DB.Store)
                    {
                        for(const auto& Expected:Import.Values)
                        {
                            const auto Actual=DB.Store->Read(Expected.Key).Get();
                            ImportVerified&=Actual.Code==EAetherStoreCode::Found&&Actual.Value.IsSet()&&
                                Actual.Value->Revision==Expected.Revision&&Actual.Value->SchemaVersion==Expected.SchemaVersion&&
                                Actual.Value->Payload==Expected.Payload;
                        }
                        ImportVerified&=DB.Store->ImportLegacy(Import).Get().Code==EAetherStoreCode::Replayed;
                        DB.Store->Close();
                    }
                    if(!ImportVerified)ImportDetail=TEXT("Imported database failed reopen verification; retain backup and do not activate");
                }
            }
            else ImportDetail=DB.Detail;
            Root->SetStringField(TEXT("databasePath"),DatabasePath);
        }
    }
    Root->SetBoolField(TEXT("finalSchemaConverted"),SnapshotConverted);
    Root->SetBoolField(TEXT("worldConverted"),SnapshotConverted);
    Root->SetBoolField(TEXT("databaseWritten"),DatabaseWritten);
    Root->SetBoolField(TEXT("importVerified"),ImportVerified);
    Root->SetBoolField(TEXT("activated"),false);
    Root->SetStringField(TEXT("snapshotConversionDetail"),SnapshotDetail);
    Root->SetStringField(TEXT("importDetail"),ImportDetail);
    Root->SetBoolField(TEXT("legacyValid"), Valid);
    Root->SetBoolField(TEXT("profilesConverted"), ProfilesConverted);
    Root->SetStringField(TEXT("profileConversionDetail"), ConversionDetail);
    Root->SetStringField(TEXT("detail"), Result.Detail);
    FString Json;
    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json))) return 2;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Report), true);
    if (!FFileHelper::SaveStringToFile(Json, *Report, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
        &IFileManager::Get(), FILEWRITE_NoReplaceExisting)) return 2;
    return Valid && SnapshotConverted && (!ImportRequested || ImportVerified) ? 0 : 1;
}
