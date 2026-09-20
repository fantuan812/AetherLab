#pragma once
#include "CoreMinimal.h"
#include "AetherV10Packets.generated.h"
class UPackageMap;

namespace AetherV10Network
{
    inline constexpr int32 CommandBytes=1024,ReplyBytes=16384,ChunkBytes=16384,SnapshotBytes=4*1024*1024;
}
// 所有数组在 NetSerialize 分配前检查长度，不能等 RPC 实现收到巨量 TArray 后才限流。
USTRUCT()
struct AETHERLAB_API FAetherV10CommandPacket
{
    GENERATED_BODY()
    FGuid Channel;
    TArray<uint8> Bytes;
    bool NetSerialize(FArchive& Ar,UPackageMap*,bool& Success);
};
USTRUCT()
struct AETHERLAB_API FAetherV10ReplyPacket
{
    GENERATED_BODY()
    FGuid Channel;
    TArray<uint8> Bytes;
    bool NetSerialize(FArchive& Ar,UPackageMap*,bool& Success);
};
USTRUCT()
struct AETHERLAB_API FAetherV10SnapshotChunk
{
    GENERATED_BODY()
    FGuid Channel,Transfer;
    int64 Revision=-1;
    uint32 Total=0,Offset=0,Checksum=0;
    TArray<uint8> Bytes;
    bool NetSerialize(FArchive& Ar,UPackageMap*,bool& Success);
};
template<> struct TStructOpsTypeTraits<FAetherV10CommandPacket>:TStructOpsTypeTraitsBase2<FAetherV10CommandPacket>{enum{WithNetSerializer=true};};
template<> struct TStructOpsTypeTraits<FAetherV10ReplyPacket>:TStructOpsTypeTraitsBase2<FAetherV10ReplyPacket>{enum{WithNetSerializer=true};};
template<> struct TStructOpsTypeTraits<FAetherV10SnapshotChunk>:TStructOpsTypeTraitsBase2<FAetherV10SnapshotChunk>{enum{WithNetSerializer=true};};

// 每个连接独立的双桶限制；服务器单调时间，不接受客户端时间或请求声称的成本。
struct FAetherV10RequestBudget
{
    double Tokens=12,Bytes=12288,Last=-1;
    bool Consume(double Now,int32 Size,double Rate=6,double Burst=12);
};
