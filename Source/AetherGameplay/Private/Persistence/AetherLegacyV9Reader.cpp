#include "Persistence/AetherLegacyV9Reader.h"
#include "Misc/EngineVersion.h"
#include "Misc/SecureHash.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CustomVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/NameAsStringProxyArchive.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchiveAdapters.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr int64 MaxFileBytes = 16 * 1024 * 1024;
constexpr int32 MaxStringBytes = 512;

// FMemoryReader 默认的 Seek 对越界使用 check；迁移必须返回诊断，不能终止服务器。
class FBoundedReader final : public FArchive
{
    const TArray<uint8>& Bytes;
    int64 Position = 0;
public:
    explicit FBoundedReader(const TArray<uint8>& InBytes) : Bytes(InBytes)
    { SetIsLoading(true); SetIsPersistent(true); ArMaxSerializeSize = MaxStringBytes; }
    int64 Tell() override { return Position; }
    int64 TotalSize() override { return Bytes.Num(); }
    bool AtEnd() override { return Position == Bytes.Num(); }
    void Serialize(void* Data, int64 Size) override
    {
        if (IsError() || Size < 0 || Size > TotalSize() - Tell())
        {
            SetCriticalError();
            // 固定宽度字段读失败后归零，避免调用方使用未初始化计数。
            if (Size > 0 && Size <= MaxStringBytes) FMemory::Memzero(Data, Size);
            return;
        }
        if (Size > 0) FMemory::Memcpy(Data, Bytes.GetData() + Position, Size);
        Position += Size;
    }
    void Seek(int64 NewPosition) override
    {
        if (NewPosition < 0 || NewPosition > TotalSize()) { SetCriticalError(); return; }
        Position = NewPosition;
    }
};

// 在 UE 分配 TArray/TMap 之前扫描完整标签流。仅限制文件总长不足以防止损坏的
// 数量字段触发巨额分配。类型树也逐节点检查，不能交给引擎无限展开不可信 InnerCount。
class FTagScanner
{
    FNameAsStringProxyArchive& Ar;
    int64 Limit;
    int32 Nodes = 0;
    bool Fail(const FString& Why) { Detail = Why; return false; }
    bool Type(UE::FPropertyTypeName Expected, int32 Depth)
    {
        if (Depth > 8 || ++Nodes > 32) return Fail(TEXT("Property type tree exceeds v9 bounds"));
        FName Name; int32 Count = 0; Ar << Name << Count;
        if (Ar.IsError() || Name != Expected.GetName() || Count != Expected.GetParameterCount())
            return Fail(TEXT("Property type does not match frozen v9 layout"));
        for (int32 I = 0; I < Count; ++I) if (!Type(Expected.GetParameter(I), Depth + 1)) return false;
        return true;
    }
    bool Count(int32& Out, int32 Maximum)
    {
        Ar << Out;
        return !Ar.IsError() && Out >= 0 && Out <= Maximum && Out <= Limit - Ar.Tell();
    }
    int32 Capacity(FName Name) const
    {
        if (Name == TEXT("Profiles") || Name == TEXT("Loot") || Name == TEXT("Items")) return 128;
        if (Name == TEXT("Inventory") || Name == TEXT("CampReceipts")) return 32;
        if (Name == TEXT("InventoryReceipts") || Name == TEXT("ServiceReceipts") || Name == TEXT("DailyEvidence")) return 64;
        if (Name == TEXT("DailyClaims")) return 16;
        if (Name == TEXT("Equipped")) return 10;
        if (Name == TEXT("World")) return 4096;
        return 512;
    }
    bool Value(FProperty* P, int32 Depth)
    {
        if (Depth > 16 || Ar.Tell() > Limit || Ar.IsError()) return Fail(TEXT("Invalid nested property boundary"));
        if (auto* A = CastField<FArrayProperty>(P))
        {
            int32 N = 0;
            if (!Count(N, Capacity(P->GetFName()))) return Fail(TEXT("Array count exceeds v9 bounds: ") + P->GetName());
            for (int32 I = 0; I < N; ++I) if (!Value(A->Inner, Depth + 1)) return false;
            return true;
        }
        if (auto* M = CastField<FMapProperty>(P))
        {
            // SaveGame 是全量快照；默认 map 的差量删除列表必须为空，-1 表示整表替换。
            int32 Removed = 0, N = 0; Ar << Removed;
            if (Ar.IsError() || (Removed != 0 && Removed != -1) || !Count(N, Capacity(P->GetFName())))
                return Fail(TEXT("Invalid snapshot map count"));
            for (int32 I = 0; I < N; ++I)
                if (!Value(M->KeyProp, Depth + 1) || !Value(M->ValueProp, Depth + 1)) return false;
            return true;
        }
        if (auto* S = CastField<FStructProperty>(P))
        {
            if (!(S->Struct->StructFlags & STRUCT_SerializeNative)) return Struct(S->Struct, Depth + 1);
            // v9 唯一允许的 native 值没有指针、动态数组或对象加载行为。
            const FName Name = S->Struct->GetFName();
            if (Name != TEXT("Guid") && Name != TEXT("Vector") && Name != TEXT("Quat") &&
                Name != TEXT("Rotator") && Name != TEXT("Transform"))
                return Fail(TEXT("Unknown native v9 struct: ") + Name.ToString());
        }
        else if (!CastField<FNumericProperty>(P) && !CastField<FEnumProperty>(P) &&
            !CastField<FNameProperty>(P) && !CastField<FStrProperty>(P) && !CastField<FBoolProperty>(P))
            return Fail(TEXT("Object references and unknown field types are not part of v9 saves"));

        // 标量/native 值使用引擎的固定版本编码；已明确排除含集合或 UObject 的类型。
        if (P->GetSize() <= 0 || P->GetSize() > 4096) return Fail(TEXT("Invalid scalar allocation"));
        void* Temp = FMemory::Malloc(P->GetSize(), P->GetMinAlignment());
        P->InitializeValue(Temp);
        P->SerializeItem(FStructuredArchiveFromArchive(Ar).GetSlot(), Temp);
        P->DestroyValue(Temp); FMemory::Free(Temp);
        return !Ar.IsError() && Ar.Tell() <= Limit;
    }
public:
    FString Detail;
    FTagScanner(FNameAsStringProxyArchive& InAr, int64 End) : Ar(InAr), Limit(End) {}
    bool Struct(const UStruct* Layout, int32 Depth = 0)
    {
        if (Depth == 0)
        {
            // UE 5.8 仅在 UClass 根写此控制字节，嵌套 USTRUCT 没有。
            uint8 Control = 0; Ar << Control;
            if (Ar.IsError() || Control != 0) return Fail(TEXT("Unsupported class serialization extension"));
        }
        TSet<FName> Seen;
        while (!Ar.IsError() && Ar.Tell() < Limit && Seen.Num() < 128)
        {
            FName Name; Ar << Name;
            if (Ar.IsError()) return Fail(FString::Printf(TEXT("Truncated property name at %lld in %s"), Ar.Tell(), *Layout->GetPathName()));
            if (Name.IsNone()) return true; // 旧写入器省略与冻结 CDO 相同的字段，包括 Version=5。
            FProperty* P = FindFProperty<FProperty>(Layout, Name);
            if (!P || Seen.Contains(Name)) return Fail(TEXT("Unknown or duplicate v9 property: ") + Name.ToString());
            Seen.Add(Name); Nodes = 0;
            if (!Type(UE::FPropertyTypeName(P), 0)) return false;
            int32 Size = 0; uint8 Flags = 0; Ar << Size << Flags;
            // 冻结写入器未使用可覆盖属性扩展、跳过值或未知标志。
            if (Ar.IsError() || (Flags & ~uint8(0x1B))) return Fail(TEXT("Unsupported v9 property flags"));
            if (Flags & 1) { int32 Index = 0; Ar << Index; if (Index != 0) return Fail(TEXT("Invalid static array index")); }
            if (Flags & 2) { FGuid Guid; Ar << Guid; }
            const int64 End = Ar.Tell() + int64(Size);
            if (Ar.IsError() || Size < 0 || End > Limit) return Fail(TEXT("Property size exceeds file boundary"));
            if (CastField<FBoolProperty>(P))
            {
                if (Size != 0) return Fail(TEXT("Tagged bool must have no payload"));
            }
            else
            {
                const int64 OuterLimit = Limit; Limit = End;
                const bool Valid = Value(P, Depth + 1); Limit = OuterLimit;
                if (!Valid || Ar.Tell() != End) return Fail(Detail.IsEmpty() ? TEXT("Property payload size mismatch: ") + Name.ToString() : Detail);
            }
        }
        return Fail(TEXT("Missing property terminator or too many fields"));
    }
};

bool ValidateSnapshot(const UAetherFrontierSave& Save, FString& Detail)
{
    // 新内容定义可以改变容量/槽位/门槛；旧档必须先按冻结 v9 规则证明自身合法。
    static const FAetherRules Rules = []
    {
        FString Text;
        FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/Legacy/V9Rules.json")));
        return FAetherRules::Parse(Text);
    }();
    if (!Rules.bValid) { Detail = TEXT("Frozen v9 rule definitions are unavailable"); return false; }
    if (!FMath::IsFinite(Save.RainKgPerM2Sec) || Save.RainKgPerM2Sec < 0 ||
        !FMath::IsFinite(Save.AmbientTemperatureC) || Save.WindMPerSec.ContainsNaN())
    { Detail = TEXT("Invalid legacy weather state"); return false; }
    if ((Save.Version != 4 && Save.Version != 5) || Save.Generation < 0 ||
        Save.Profiles.Num() > 128 || !Save.ValidateWorldLedger(Rules))
    { Detail = TEXT("Unsupported save schema or invalid world ledger"); return false; }
    TSet<FString> Ids;
    for (const auto& P : Save.Profiles)
    {
        if (!P.Validate(Rules) || P.CharacterId.IsEmpty() || Ids.Contains(P.CharacterId))
        { Detail = TEXT("Invalid or duplicate legacy profile"); return false; }
        Ids.Add(P.CharacterId);
    }
    TSet<FName> WorldIds;
    for (const auto& R : Save.World)
    {
        if (R.StableId.IsNone() || WorldIds.Contains(R.StableId) || !R.Transform.IsValid() || R.Transform.GetLocation().GetAbsMax() > 1.e8 ||
            R.Transform.GetScale3D().GetMin() <= 0 || R.Transform.GetScale3D().GetMax() > 1000 ||
            (R.MaterialSchema != 0 && R.MaterialSchema != 1) ||
            !FMath::IsFinite(R.RemainingEnergyJ) || R.RemainingEnergyJ < 0 ||
            !FMath::IsFinite(R.SourceAge) || R.SourceAge < 0 ||
            !FMath::IsFinite(R.ElectricalWaterKg) || R.ElectricalWaterKg < 0 || R.ElectricalWaterKg > R.WaterKg ||
            !FMath::IsFinite(R.ElectricalWetness01) || R.ElectricalWetness01 < 0 || R.ElectricalWetness01 > 1 ||
            !FMath::IsFinite(R.GasEnergyJ) || R.GasEnergyJ < 0 || R.GasEnergyJ > 1.e12 ||
            (R.bBurst && R.GasEnergyJ > 0) || (R.bBroken != (R.Integrity <= .05)) ||
            !FMath::IsFinite(R.EnthalpyJ) || FMath::Abs(R.EnthalpyJ) > 1.e12 || !FMath::IsFinite(R.WaterKg) || R.WaterKg < 0 ||
            !FMath::IsFinite(R.FuelKg) || R.FuelKg < 0 || !FMath::IsFinite(R.Integrity) || R.Integrity < 0 || R.Integrity > 1)
        { Detail = TEXT("Invalid or duplicate legacy world record"); return false; }
        WorldIds.Add(R.StableId);
    }
    return true;
}
}

FString AetherLegacyV9::LayoutFingerprint()
{
    TSet<const UStruct*> Seen;
    TArray<FString> Lines;
    TFunction<void(const UStruct*)> VisitStruct;
    TFunction<void(FProperty*)> VisitProperty;
    VisitProperty = [&](FProperty* P)
    {
        if (auto* S = CastField<FStructProperty>(P)) VisitStruct(S->Struct);
        if (auto* A = CastField<FArrayProperty>(P)) VisitProperty(A->Inner);
        if (auto* M = CastField<FMapProperty>(P)) { VisitProperty(M->KeyProp); VisitProperty(M->ValueProp); }
    };
    VisitStruct = [&](const UStruct* S)
    {
        if (Seen.Contains(S)) return;
        Seen.Add(S);
        // 标签写入会省略默认值；因此冻结字段类型还不够，必须同时冻结 CDO/struct 默认。
        const UScriptStruct* Script = Cast<UScriptStruct>(S);
        void* DefaultData = Script ? FMemory::Malloc(Script->GetStructureSize(), Script->GetMinAlignment())
            : static_cast<void*>(GetMutableDefault<UAetherFrontierSave>());
        if (Script) Script->InitializeStruct(DefaultData);
        for (TFieldIterator<FProperty> It(S); It; ++It)
        {
            FProperty* P = *It;
            FString DefaultText;
            P->ExportTextItem_Direct(DefaultText, P->ContainerPtrToValuePtr<void>(DefaultData), nullptr, nullptr, PPF_None);
            Lines.Add(S->GetPathName() + TEXT("|") + P->GetName() + TEXT("|") +
                FString(WriteToString<256>(UE::FPropertyTypeName(P))) + TEXT("|") + FString::FromInt(P->ArrayDim) + TEXT("|") + DefaultText);
            VisitProperty(P);
        }
        if (Script) { Script->DestroyStruct(DefaultData); FMemory::Free(DefaultData); }
    };
    VisitStruct(UAetherFrontierSave::StaticClass());
    Lines.Sort();
    return FMD5::HashAnsiString(*FString::Join(Lines, TEXT("\n")));
}

FAetherLegacyV9ReadResult AetherLegacyV9::Read(const TArray<uint8>& Bytes)
{
    check(IsInGameThread()); // 临时 UObject 只在游戏线程创建，工作线程只接收转换后的 DTO。
    FAetherLegacyV9ReadResult Out;
    if (LayoutFingerprint() != TEXT("c95976b1682573e91fd1428684ee763e"))
    { Out.Code = EAetherLegacyReadCode::Unsupported; Out.Detail = TEXT("Frozen v9 reflection layout changed; provide a versioned reader"); return Out; }
    if (Bytes.Num() < 64 || Bytes.Num() > MaxFileBytes)
    { Out.Detail = TEXT("Legacy file size is outside supported bounds"); return Out; }
    FBoundedReader Reader(Bytes);
    int32 Magic = 0, HeaderVersion = 0; Reader << Magic << HeaderVersion;
    if (Magic != 0x53415647 || HeaderVersion != 3)
    { Out.Code = EAetherLegacyReadCode::Unsupported; Out.Detail = TEXT("Expected GVAS header version 3"); return Out; }
    FPackageFileVersion PackageVersion; FEngineVersion EngineVersion;
    Reader << PackageVersion << EngineVersion;
    if (Reader.IsError() || PackageVersion != GPackageFileUEVersion ||
        EngineVersion.GetMajor() != 5 || EngineVersion.GetMinor() != 8)
    { Out.Code = EAetherLegacyReadCode::Unsupported; Out.Detail = TEXT("No frozen reader for this engine serialization version"); return Out; }
    Reader.SetUEVer(PackageVersion); Reader.SetEngineVer(EngineVersion);
    int32 Format = 0, Versions = 0; Reader << Format;
    const int64 VersionsStart = Reader.Tell(); Reader << Versions;
    if (Reader.IsError() || Format != int32(ECustomVersionSerializationFormat::Optimized) ||
        Versions < 0 || Versions > 1024 || int64(Versions) * 20 > Reader.TotalSize() - Reader.Tell())
    { Out.Detail = TEXT("Invalid legacy custom version header"); return Out; }
    Reader.Seek(VersionsStart);
    FCustomVersionContainer CustomVersions;
    CustomVersions.Serialize(Reader, ECustomVersionSerializationFormat::Optimized);
    Reader.SetCustomVersions(CustomVersions);
    FString ClassName; Reader << ClassName;
    if (Reader.IsError() || (ClassName != TEXT("/Script/AetherLab.AetherFrontierSave") && ClassName != TEXT("/Script/AetherGameplay.AetherFrontierSave")))
    { Out.Code = EAetherLegacyReadCode::Unsupported; Out.Detail = TEXT("Legacy class is not whitelisted"); return Out; }
    const int64 PayloadStart = Reader.Tell();
    FNameAsStringProxyArchive ScanArchive(Reader);
    FTagScanner Scanner(ScanArchive, Reader.TotalSize());
    if (!Scanner.Struct(UAetherFrontierSave::StaticClass()))
    { Out.Detail = Scanner.Detail; return Out; }
    int32 ObjectGuidFlag = 0; Reader << ObjectGuidFlag;
    if (Reader.IsError() || ObjectGuidFlag != 0 || Reader.Tell() != Reader.TotalSize())
    { Out.Detail = TEXT("Invalid legacy object trailer"); return Out; }
    // 所有属性和尾部已精确消费，才允许 UE 分配集合并反序列化冻结类型。
    Reader.Seek(PayloadStart);
    Out.Snapshot.Reset(NewObject<UAetherFrontierSave>());
    FObjectAndNameAsStringProxyArchive Archive(Reader, false);
    Out.Snapshot->Serialize(Archive);
    if (Reader.IsError() || Archive.IsError() || Reader.Tell() != Reader.TotalSize() ||
        !ValidateSnapshot(*Out.Snapshot, Out.Detail))
    {
        Out.Snapshot.Reset();
        if (Out.Detail.IsEmpty()) Out.Detail = FString::Printf(TEXT("Legacy payload boundary/error: %lld/%lld reader=%d proxy=%d"), Reader.Tell(), Reader.TotalSize(), Reader.IsError(), Archive.IsError());
        return Out;
    }
    Out.Code = EAetherLegacyReadCode::Ready;
    return Out;
}
