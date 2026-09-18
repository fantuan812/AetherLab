#include "World/AetherContainerCodec.h"
#include "Inventory/AetherInventoryCodec.h"
namespace
{
struct FWriter
{
    TArray<uint8> Bytes;bool Valid=true;
    void UInt(uint64 V,int32 N){for(int32 I=0;I<N;++I)Bytes.Add(uint8(V>>(I*8)));}
    void Double(double V){uint64 Bits;FMemory::Memcpy(&Bits,&V,8);UInt(Bits,8);}
    void Text(const FString& S)
    {
        FTCHARToUTF8 U(*S);FUTF8ToTCHAR Back(U.Get(),U.Length());
        if(U.Length()>512||FString(Back.Length(),Back.Get())!=S){Valid=false;return;}
        UInt(U.Length(),2);Bytes.Append(reinterpret_cast<const uint8*>(U.Get()),U.Length());
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
    double Double(){const uint64 Bits=UInt(8);double V;FMemory::Memcpy(&V,&Bits,8);if(!FMath::IsFinite(V))Valid=false;return Valid?V:0;}
    FString Text(int32 MaxChars)
    {
        const int32 N=int32(UInt(2));if(!Valid||N>512||N>Bytes.Num()-Offset){Valid=false;return {};}
        FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()+Offset),N);
        FString S(Decoded.Length(),Decoded.Get());FTCHARToUTF8 Back(*S);
        if(S.Len()>MaxChars||Back.Length()!=N||FMemory::Memcmp(Back.Get(),Bytes.GetData()+Offset,N)!=0)Valid=false;
        Offset+=N;return S;
    }
};
}
bool AetherContainerCodec::Encode(const FAetherContainerStateV10& S,const FAetherV10ItemDefinitions& D,TArray<uint8>& Bytes,FString& Reason)
{
    Bytes.Reset();if(!S.Validate(D,Reason))return false;
    // 容器可以有独立容量，但装备定义/堆叠上限仍只有同一份权威注册表。
    auto ContainerDefinitions=D;ContainerDefinitions.DefaultCapacity=S.Inventory.Capacity;TArray<uint8> Inventory;
    if(!AetherInventoryCodec::Encode(S.Inventory,ContainerDefinitions,Inventory,Reason))return false;
    FWriter W;W.UInt(0x544e4341,4);W.UInt(SchemaVersion,2);W.UInt(D.ContentSchemaVersion,2);
    W.Text(S.ContainerId);W.UInt(S.Revision,8);W.UInt(uint8(S.Kind),1);W.Text(S.OwnerCharacterId);W.Text(S.RegionId);
    W.Double(S.Location.X);W.Double(S.Location.Y);W.Double(S.Location.Z);W.UInt(S.bActive?1:0,1);
    W.UInt(S.Inventory.Capacity,2);W.UInt(Inventory.Num(),4);W.Bytes.Append(Inventory);
    if(!W.Valid||W.Bytes.Num()>MaxBytes){Reason=TEXT("Container cannot be encoded losslessly within size limit");return false;}
    Bytes=MoveTemp(W.Bytes);return true;
}
bool AetherContainerCodec::Decode(const TArray<uint8>& Bytes,const FAetherV10ItemDefinitions& D,FAetherContainerStateV10& Out,FString& Reason)
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(Bytes.Num()<16||Bytes.Num()>MaxBytes)return Fail(TEXT("Invalid container DTO size"));
    FReader R{Bytes};FAetherContainerStateV10 S;
    if(R.UInt(4)!=0x544e4341||R.UInt(2)!=SchemaVersion||R.UInt(2)!=uint64(D.ContentSchemaVersion))return Fail(TEXT("Unknown container DTO/content schema"));
    S.ContainerId=R.Text(96);const uint64 Version=R.UInt(8);
    if(Version>=uint64(MAX_int64))return Fail(TEXT("Invalid container version"));S.Revision=int64(Version);
    S.Kind=EAetherContainerKind(R.UInt(1));S.OwnerCharacterId=R.Text(32);S.RegionId=R.Text(96);
    const double X=R.Double(),Y=R.Double(),Z=R.Double();S.Location=FVector(X,Y,Z);
    const auto Active=R.UInt(1);if(Active>1)return Fail(TEXT("Invalid active flag"));S.bActive=Active!=0;
    const int32 Capacity=int32(R.UInt(2));if(Capacity<1||Capacity>256)return Fail(TEXT("Invalid container capacity"));
    const uint64 Size=R.UInt(4);
    if(!R.Valid||Size>AetherInventoryCodec::MaxBytes||Size!=uint64(Bytes.Num()-R.Offset))return Fail(TEXT("Truncated/oversized/trailing container inventory"));
    TArray<uint8> Inventory;Inventory.Append(Bytes.GetData()+R.Offset,int32(Size));
    auto ContainerDefinitions=D;ContainerDefinitions.DefaultCapacity=Capacity;
    if(!AetherInventoryCodec::Decode(Inventory,ContainerDefinitions,S.Inventory,Reason)||!S.Validate(D,Reason))return false;
    Out=MoveTemp(S);return true;
}
