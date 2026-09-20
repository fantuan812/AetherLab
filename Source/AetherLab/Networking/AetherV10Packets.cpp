#include "Networking/AetherV10Packets.h"
#include "Serialization/Archive.h"
namespace
{
bool Payload(FArchive& Ar,TArray<uint8>& Bytes,uint32 Maximum,bool& Success)
{
    uint32 Count=Ar.IsSaving()?uint32(Bytes.Num()):0;
    if(Count>Maximum){Success=false;Ar.SetError();return false;}
    Ar.SerializeInt(Count,Maximum+1);
    if(Ar.IsError()||Count==0||Count>Maximum){Success=false;Ar.SetError();return false;}
    if(Ar.IsLoading())Bytes.SetNumUninitialized(int32(Count));
    Ar.Serialize(Bytes.GetData(),Count);Success=!Ar.IsError();return Success;
}
}
bool FAetherV10CommandPacket::NetSerialize(FArchive& Ar,UPackageMap*,bool& Success)
{Ar<<Channel;return Payload(Ar,Bytes,AetherV10Network::CommandBytes,Success);}
bool FAetherV10ReplyPacket::NetSerialize(FArchive& Ar,UPackageMap*,bool& Success)
{Ar<<Channel;return Payload(Ar,Bytes,AetherV10Network::ReplyBytes,Success);}
bool FAetherV10SnapshotChunk::NetSerialize(FArchive& Ar,UPackageMap*,bool& Success)
{
    Ar<<Channel<<Transfer<<Revision<<Total<<Offset<<Checksum<<Kind<<Context<<WorldRevision;
    if(Kind>1||(Kind==0?(Context.IsValid()||WorldRevision!=-1):(!Context.IsValid()||WorldRevision<0||WorldRevision==MAX_int64)))
    {Success=false;Ar.SetError();return false;}
    if(!Channel.IsValid()||!Transfer.IsValid()||Revision<0||Revision==MAX_int64||Total==0||Total>AetherV10Network::SnapshotBytes||Offset>=Total)
    {Success=false;Ar.SetError();return false;}
    if(!Payload(Ar,Bytes,AetherV10Network::ChunkBytes,Success))return false;
    Success=uint32(Bytes.Num())<=Total-Offset;
    if(!Success)Ar.SetError();return Success;
}
bool FAetherV10RequestBudget::Consume(double Now,int32 Size,double Rate,double Burst)
{
    if(!FMath::IsFinite(Now)||Now<0||Size<0||Size>AetherV10Network::CommandBytes)return false;
    if(Last<0){Tokens=Burst;Bytes=Burst*AetherV10Network::CommandBytes;Last=Now;}
    const double Dt=FMath::Clamp(Now-Last,0.,10.);
    // 时钟倒退不能重新填满令牌；过久的停顿也不产生无界突发。
    Last=FMath::Max(Last,Now);
    Tokens=FMath::Min(Burst,Tokens+Dt*Rate);
    Bytes=FMath::Min(Burst*AetherV10Network::CommandBytes,Bytes+Dt*Rate*AetherV10Network::CommandBytes);
    if(Tokens<1||Bytes<Size)return false;
    Tokens-=1;Bytes-=Size;return true;
}

bool FAetherV10ContainerQuery::NetSerialize(FArchive& Ar,UPackageMap*,bool& Success)
{
    Ar<<Channel<<Context;uint32 Count=Ar.IsSaving()?TargetId.Len():0;
    if(Count>96){Success=false;Ar.SetError();return false;}Ar.SerializeInt(Count,97);
    if(Ar.IsError()||Count>96||!Channel.IsValid()||!Context.IsValid()){Success=false;Ar.SetError();return false;}
    if(Ar.IsLoading())TargetId.Empty(Count);
    for(uint32 I=0;I<Count;++I)
    {
        uint8 C=Ar.IsSaving()?uint8(TargetId[I]):0;Ar<<C;
        if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))
        {Success=false;Ar.SetError();return false;}
        if(Ar.IsLoading())TargetId.AppendChar(TCHAR(C));
    }
    Success=!Ar.IsError();return Success;
}
