#include "World/AetherWorldCodec.h"
namespace
{
// 磁盘格式使用显式小端数值和长度前缀 UTF-8；不持久化 FName 索引或 UE 反射布局。
struct FWriter
{
    TArray<uint8> Bytes;bool Valid=true;
    void UInt(uint64 V,int32 N){for(int32 I=0;I<N;++I)Bytes.Add(uint8(V>>(I*8)));}
    void Double(double V){uint64 Bits;FMemory::Memcpy(&Bits,&V,8);UInt(Bits,8);}
    void Float(float V){uint32 Bits;FMemory::Memcpy(&Bits,&V,4);UInt(Bits,4);}
    void Vector(const FVector& V){Double(V.X);Double(V.Y);Double(V.Z);}
    void Guid(const FGuid& G){UInt(G.A,4);UInt(G.B,4);UInt(G.C,4);UInt(G.D,4);}
    void Text(const FString& S)
    {
        FTCHARToUTF8 U(*S);FUTF8ToTCHAR Back(U.Get(),U.Length());
        if(U.Length()>512||FString(Back.Length(),Back.Get())!=S){Valid=false;return;}
        UInt(U.Length(),2);Bytes.Append(reinterpret_cast<const uint8*>(U.Get()),U.Length());
    }
    void Strings(const TArray<FString>& Values){UInt(Values.Num(),2);for(const auto& S:Values)Text(S);}
    void Encounter(const FAetherEncounterStateV10& E)
    {
        Text(E.Definition);Guid(E.Instance);UInt(E.Phase,1);UInt(E.Version,4);UInt(E.Wave,4);UInt(E.LockedSeats,1);
        Float(E.PhaseStarted);Float(E.Progress);Strings(E.Participants);Strings(E.Settled);
    }
};
struct FReader
{
    const TArray<uint8>& Bytes;int32 Offset=0;bool Valid=true;
    uint64 UInt(int32 N)
    {
        if(!Valid||N>Bytes.Num()-Offset){Valid=false;return 0;}
        uint64 V=0;for(int32 I=0;I<N;++I)V|=uint64(Bytes[Offset++])<<(I*8);return V;
    }
    int32 Int(){const auto V=UInt(4);if(V>MAX_int32)Valid=false;return Valid?int32(V):0;}
    int64 Long(){const auto V=UInt(8);if(V>=uint64(MAX_int64))Valid=false;return Valid?int64(V):0;}
    int32 Count(int32 Limit){const int32 V=int32(UInt(2));if(V>Limit)Valid=false;return Valid?V:0;}
    double Double(){const uint64 Bits=UInt(8);double V;FMemory::Memcpy(&V,&Bits,8);if(!FMath::IsFinite(V))Valid=false;return Valid?V:0;}
    float Float(){const uint32 Bits=uint32(UInt(4));float V;FMemory::Memcpy(&V,&Bits,4);if(!FMath::IsFinite(V))Valid=false;return Valid?V:0;}
    FVector Vector()
    {
        // 分开读以固定游标顺序；先拒绝 NaN，再构造 UE 数学类型，避免诊断构造器改写损坏值。
        const double X=Double(),Y=Double(),Z=Double();return FVector(X,Y,Z);
    }
    FGuid Guid(){const uint32 A=uint32(UInt(4)),B=uint32(UInt(4)),C=uint32(UInt(4)),D=uint32(UInt(4));return FGuid(A,B,C,D);}
    FString Text(int32 MaxChars)
    {
        const int32 N=int32(UInt(2));if(!Valid||N>512||N>Bytes.Num()-Offset){Valid=false;return {};}
        FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()+Offset),N);
        FString S(Decoded.Length(),Decoded.Get());FTCHARToUTF8 Check(*S);
        if(S.Len()>MaxChars||Check.Length()!=N||FMemory::Memcmp(Check.Get(),Bytes.GetData()+Offset,N)!=0)Valid=false;
        Offset+=N;return S;
    }
    TArray<FString> Strings(){TArray<FString> V;const int32 N=Count(128);for(int32 I=0;I<N&&Valid;++I)V.Add(Text(32));return V;}
    FAetherEncounterStateV10 Encounter()
    {
        FAetherEncounterStateV10 E;E.Definition=Text(128);E.Instance=Guid();E.Phase=uint8(UInt(1));
        E.Version=Int();E.Wave=Int();E.LockedSeats=int32(UInt(1));E.PhaseStarted=Float();E.Progress=Float();
        E.Participants=Strings();E.Settled=Strings();return E;
    }
};
TArray<FString> Keys(const TMap<FString,int32>& Map)
{
    TArray<FString> V;Map.GetKeys(V);V.Sort([](const auto& A,const auto& B){return A.Compare(B,ESearchCase::CaseSensitive)<0;});return V;
}
}
bool AetherWorldCodec::Encode(const FAetherWorldStateV10& S,const FAetherV10ItemDefinitions& Items,
    const FAetherRules& Rules,const TMap<FString,int64>& Profiles,TArray<uint8>& Bytes,FString& Reason)
{
    Bytes.Reset();if(!S.Validate(Items,Rules,Profiles,Reason))return false;
    FWriter W;W.UInt(0x444C5741,4);W.UInt(SchemaVersion,2);W.UInt(Items.ContentSchemaVersion,2);W.UInt(S.Revision,8);
    W.UInt((S.bSupplyRestored?1:0)|(S.bWorkshopRestored?2:0)|(S.bBridgeReleased?4:0)|(S.bPowerOn?8:0),1);
    W.Double(S.RainKgPerM2Sec);W.Double(S.AmbientTemperatureC);W.Vector(S.WindMPerSec);
    // 数组顺序保留旧快照；无顺序语义的字典按区分大小写的 ID 排序，重试产生一致字节。
    W.UInt(S.Bodies.Num(),2);
    for(const auto& B:S.Bodies)
    {
        W.Text(B.RegionId);W.Text(B.StableId);W.Vector(B.Transform.GetTranslation());
        const auto Q=B.Transform.GetRotation();W.Double(Q.X);W.Double(Q.Y);W.Double(Q.Z);W.Double(Q.W);
        W.Vector(B.Transform.GetScale3D());
        W.UInt((B.bGateOpen?1:0)|(B.bHasMechanism?2:0)|(B.bSupportReleased?4:0)|(B.bSourceEnabled?8:0)|
            (B.bBurning?16:0)|(B.bBroken?32:0)|(B.bBurst?64:0),1);
        W.Double(B.RemainingEnergyJ);W.Double(B.SourceAge);W.UInt(B.MaterialSignature,4);W.UInt(B.MaterialSchema,1);
        W.Double(B.EnthalpyJ);W.Double(B.WaterKg);W.Double(B.ElectricalWaterKg);W.Double(B.ElectricalWetness01);
        W.Double(B.FuelKg);W.Double(B.Integrity);W.Double(B.GasEnergyJ);
    }
    W.UInt(S.Loot.Num(),2);
    for(const auto& L:S.Loot)
    {
        W.Guid(L.ClaimId);W.Vector(L.Location);W.Text(L.Definition);W.Text(L.ClaimedBy);W.UInt(L.Count,4);
        const auto Sorted=Keys(L.Items);W.UInt(Sorted.Num(),2);for(const auto& K:Sorted){W.Text(K);W.UInt(L.Items[K],4);}
    }
    W.UInt(S.CampReceipts.Num(),2);for(const auto& C:S.CampReceipts){W.Text(C.Definition);W.Guid(C.Instance);W.UInt(C.RespawnAfterUtc,8);}
    W.UInt(S.LegacyServiceReceipts.Num(),2);
    for(const auto& R:S.LegacyServiceReceipts){W.Guid(R.CommandId);W.Text(R.TargetId);W.Text(R.CharacterId);W.UInt(R.ExpectedRevision,4);}
    TArray<FString> Facts;S.WorldFactSources.GetKeys(Facts);Facts.Sort([](const auto& A,const auto& B){return A.Compare(B,ESearchCase::CaseSensitive)<0;});
    W.UInt(Facts.Num(),2);for(const auto& K:Facts){W.Text(K);W.Text(S.WorldFactSources[K]);}
    W.Encounter(S.Abbey);W.Encounter(S.Relay);W.UInt(S.LegacySaveSchema,1);W.UInt(S.LegacyGeneration,4);W.Text(S.LegacySourceSha256);
    if(S.RealmId.IsValid()){W.UInt(0x314D4C52,4);W.Guid(S.RealmId);}
    if(!W.Valid||W.Bytes.Num()>MaxBytes){Reason=TEXT("World cannot be encoded losslessly within size limit");return false;}
    Bytes=MoveTemp(W.Bytes);return true;
}
bool AetherWorldCodec::Decode(const TArray<uint8>& Bytes,const FAetherV10ItemDefinitions& Items,
    const FAetherRules& Rules,const TMap<FString,int64>& Profiles,FAetherWorldStateV10& Out,FString& Reason)
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(Bytes.Num()<16||Bytes.Num()>MaxBytes)return Fail(TEXT("World DTO size outside bounds"));
    FReader R{Bytes};FAetherWorldStateV10 S;
    if(R.UInt(4)!=0x444C5741||R.UInt(2)!=SchemaVersion||R.UInt(2)!=uint64(Items.ContentSchemaVersion))return Fail(TEXT("Unknown world DTO/content schema"));
    S.Revision=R.Long();const auto Flags=R.UInt(1);if(Flags>15)return Fail(TEXT("Unknown world flags"));
    S.bSupplyRestored=(Flags&1)!=0;S.bWorkshopRestored=(Flags&2)!=0;S.bBridgeReleased=(Flags&4)!=0;S.bPowerOn=(Flags&8)!=0;
    S.RainKgPerM2Sec=R.Double();S.AmbientTemperatureC=R.Double();S.WindMPerSec=R.Vector();
    int32 N=R.Count(4096);
    for(int32 I=0;I<N&&R.Valid;++I)
    {
        FAetherReactiveRecordV10 B;B.RegionId=R.Text(128);B.StableId=R.Text(128);const FVector Position=R.Vector();
        const double X=R.Double(),Y=R.Double(),Z=R.Double(),W=R.Double();const FVector Scale=R.Vector();
        const FQuat Q(X,Y,Z,W);
        // 不归一化不合法四元数：修复输入会掩盖损坏，必须在发布候选前明确拒绝。
        if(!R.Valid||!Q.IsNormalized())return Fail(TEXT("Invalid body transform encoding"));
        B.Transform=FTransform(Q,Position,Scale);
        const auto F=R.UInt(1);if(F>127)return Fail(TEXT("Unknown body flags"));
        B.bGateOpen=(F&1)!=0;B.bHasMechanism=(F&2)!=0;B.bSupportReleased=(F&4)!=0;B.bSourceEnabled=(F&8)!=0;
        B.bBurning=(F&16)!=0;B.bBroken=(F&32)!=0;B.bBurst=(F&64)!=0;
        B.RemainingEnergyJ=R.Double();B.SourceAge=R.Double();B.MaterialSignature=uint32(R.UInt(4));B.MaterialSchema=int32(R.UInt(1));
        B.EnthalpyJ=R.Double();B.WaterKg=R.Double();B.ElectricalWaterKg=R.Double();B.ElectricalWetness01=R.Double();
        B.FuelKg=R.Double();B.Integrity=R.Double();B.GasEnergyJ=R.Double();S.Bodies.Add(MoveTemp(B));
    }
    N=R.Count(128);
    for(int32 I=0;I<N&&R.Valid;++I)
    {
        FAetherWorldLootV10 L;L.ClaimId=R.Guid();L.Location=R.Vector();L.Definition=R.Text(96);L.ClaimedBy=R.Text(32);L.Count=R.Int();
        const int32 Count=R.Count(128);
        for(int32 J=0;J<Count&&R.Valid;++J)
        {
            const auto Key=R.Text(96);const int32 Value=R.Int();if(L.Items.Contains(Key))return Fail(TEXT("Duplicate world loot definition"));L.Items.Add(Key,Value);
        }
        S.Loot.Add(MoveTemp(L));
    }
    N=R.Count(32);for(int32 I=0;I<N&&R.Valid;++I){FAetherCampReceiptV10 C;C.Definition=R.Text(128);C.Instance=R.Guid();C.RespawnAfterUtc=R.Long();S.CampReceipts.Add(MoveTemp(C));}
    N=R.Count(64);
    for(int32 I=0;I<N&&R.Valid;++I)
    {
        FAetherLegacyServiceReceiptV9 V;V.CommandId=R.Guid();V.TargetId=R.Text(128);V.CharacterId=R.Text(32);V.ExpectedRevision=R.Int();S.LegacyServiceReceipts.Add(MoveTemp(V));
    }
    N=R.Count(512);
    for(int32 I=0;I<N&&R.Valid;++I)
    {
        const auto Key=R.Text(128),Value=R.Text(128);if(S.WorldFactSources.Contains(Key))return Fail(TEXT("Duplicate world fact"));S.WorldFactSources.Add(Key,Value);
    }
    S.Abbey=R.Encounter();S.Relay=R.Encounter();S.LegacySaveSchema=int32(R.UInt(1));S.LegacyGeneration=R.Int();S.LegacySourceSha256=R.Text(64);
    if(R.Valid&&R.Offset<Bytes.Num())
    {
        if(R.UInt(4)!=0x314D4C52)return Fail(TEXT("Unknown world extension"));
        S.RealmId=R.Guid();if(!S.RealmId.IsValid())return Fail(TEXT("Invalid world realm identity"));
    }
    if(!R.Valid||R.Offset!=Bytes.Num())return Fail(TEXT("Truncated, oversized or trailing world DTO"));
    if(!S.Validate(Items,Rules,Profiles,Reason))return false;
    // 唯一发布点：任何字节、领域不变量或跨角色回执验证失败，都不改变调用者状态。
    Out=MoveTemp(S);return true;
}
