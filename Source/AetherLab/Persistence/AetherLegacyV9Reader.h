#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Persistence/AetherFrontierSave.h"

// 只读旧协议入口。返回值拥有临时 UObject；调用者必须保留此 owner，不能只缓存裸指针。
// v10 新档必须使用独立 DTO，禁止在这些 v9 反射字段上直接追加新存盘语义。
enum class EAetherLegacyReadCode : uint8 { Ready, Invalid, Unsupported };
struct FAetherLegacyV9ReadResult
{
    EAetherLegacyReadCode Code = EAetherLegacyReadCode::Invalid;
    TStrongObjectPtr<UAetherFrontierSave> Snapshot;
    FString Detail;
};
namespace AetherLegacyV9
{
    // 仅接受项目实际写过的 UE 5.8 / GVAS-3 格式和已冻结的 schema 4/5。
    // 不加载文件里指定的类，不写盘，也不在读取失败时创建空白新档。
    // 反射图指纹用于阻止把未来 v10 字段原地追加后继续假称旧 reader。
    AETHERLAB_API FString LayoutFingerprint();
    AETHERLAB_API FAetherLegacyV9ReadResult Read(const TArray<uint8>& Bytes);
}
