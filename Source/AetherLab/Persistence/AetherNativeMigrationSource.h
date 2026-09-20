#pragma once
#include "Contracts/AetherTransactionalStore.h"

enum class EAetherNativeSourceCode:uint8 {Missing,Ready,Invalid};
struct FAetherNativeMigrationSource
{
    EAetherNativeSourceCode Code=EAetherNativeSourceCode::Invalid;
    TOptional<FAetherLegacyImport> Import;
    FString BackupDirectory,Detail;
};
namespace AetherNativeMigration
{
    // 只接受 SaveGames 中命名合法的两个历史代；选择最高有效代并在导入前保存不可覆盖备份。
    // 无文件才返回 Missing，损坏/未知格式绝不能作为新游戏许可。
    AETHERLAB_API FAetherNativeMigrationSource Prepare(const FString& SavePrefix);
    AETHERLAB_API bool ValidPrefix(const FString& SavePrefix);
}
