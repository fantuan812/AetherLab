#include "Contracts/AetherPlayerCommand.h"

namespace
{
enum EField : uint32
{
    Item=1<<0, Other=1<<1, Target=1<<2, Container=1<<3, Definition=1<<4,
    Skill=1<<5, Slot=1<<6, Action=1<<7, Quantity=1<<8, Index=1<<9, Enabled=1<<10, World=1<<11
};
bool IsId(const FString& Value)
{
    if (Value.IsEmpty() || Value.Len() > 96) return false;
    for (TCHAR C : Value)
        if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
            (C >= '0' && C <= '9') || C == '_' || C == '-' || C == '.')) return false;
    return true;
}
uint32 Fields(EAetherCommandType Type)
{
    using E=EAetherCommandType;
    switch (Type)
    {
    case E::UseItem: case E::UnequipItem: return Item;
    case E::EquipItem: return Item|Slot;
    case E::SplitStack: return Item|Quantity|Index;
    case E::MergeStack: return Item|Other|Quantity;
    case E::BuyItem: return Target|Definition|Quantity;
    case E::SellItem: return Target|Item|Quantity;
    case E::MoveItem: return Item|Index;
    case E::SwapItems: return Item|Other;
    case E::SetItemLock: case E::SetItemFavorite: return Item|Enabled;
    case E::SortInventory: return Enabled;
    case E::ResetSkills: return 0;
    case E::DropItem: return Item|Quantity|World;
    case E::PickUpItem: return Target|Item|Quantity|World;
    case E::TransferItem: return Target|Container|Item|Quantity|World;
    case E::RepairItem: return Target|Item;
    case E::LearnSkill: case E::UpgradeSkill: return Skill;
    case E::BindSkill: return Skill|Slot;
    case E::ExecuteInteraction: return Target|Action|World;
    case E::ClaimReward: return Definition;
    default: return MAX_uint32;
    }
}
struct FWireWriter
{
    TArray<uint8> Bytes;
    void UInt(uint64 V, int32 N) { for(int32 I=0;I<N;++I) Bytes.Add(uint8(V>>(I*8))); }
    void Guid(const FGuid& G) { UInt(G.A,4);UInt(G.B,4);UInt(G.C,4);UInt(G.D,4); }
    void Id(const FString& S) { UInt(S.Len(),1); for(TCHAR C:S) Bytes.Add(uint8(C)); }
    void Text(const FString& S)
    {
        FTCHARToUTF8 Utf8(*S);
        UInt(Utf8.Length(),2);Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
    }
};
struct FWireReader
{
    const TArray<uint8>& Bytes;
    int32 Offset=0;
    bool Valid=true;
    uint64 UInt(int32 N)
    {
        if(!Valid || N>Bytes.Num()-Offset) {Valid=false;return 0;}
        uint64 V=0;for(int32 I=0;I<N;++I)V|=uint64(Bytes[Offset++])<<(I*8);return V;
    }
    FGuid Guid()
    {
        // 不把带游标副作用的读取放在构造函数实参中，避免不同编译器求值顺序不同。
        uint32 A=uint32(UInt(4)),B=uint32(UInt(4)),C=uint32(UInt(4)),D=uint32(UInt(4));
        return FGuid(A,B,C,D);
    }
    FString Text()
    {
        const int32 N=int32(UInt(2));
        if(!Valid || N>512 || N>Bytes.Num()-Offset){Valid=false;return {};}
        FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()+Offset),N);
        FString S(Decoded.Length(),Decoded.Get());
        FTCHARToUTF8 Encoded(*S);
        if(Encoded.Length()!=N || FMemory::Memcmp(Encoded.Get(),Bytes.GetData()+Offset,N)!=0)Valid=false;
        Offset+=N;return S;
    }
    FString Id()
    {
        const int32 N=int32(UInt(1));
        if(!Valid || N>96 || N>Bytes.Num()-Offset){Valid=false;return {};}
        FString S;S.Reserve(N);
        for(int32 I=0;I<N;++I)S.AppendChar(TCHAR(Bytes[Offset++]));
        return S;
    }
};
}

bool AetherCommands::Validate(const FAetherPlayerCommand& C, FString& Reason)
{
    const auto Reject=[&](const TCHAR* Why){Reason=Why;return false;};
    if(C.ProtocolVersion!=ProtocolVersion)return Reject(TEXT("Unsupported protocol version"));
    const uint32 Mask=Fields(C.Type);
    if(Mask==MAX_uint32)return Reject(TEXT("Unknown typed command"));
    if(C.ExpectedProfileRevision<0 || C.ExpectedProfileRevision==MAX_int64 || !C.CommandId.IsValid() ||
        (uint64(C.CommandId.A)<<32|C.CommandId.B)!=uint64(C.ExpectedProfileRevision+1) ||
        (C.CommandId.C==0 && C.CommandId.D==0))return Reject(TEXT("Invalid revision-bound command identity"));
    if(C.ExpectedWorldRevision < -1 || C.ExpectedWorldRevision==MAX_int64 ||
        ((Mask&World)!=0 ? C.ExpectedWorldRevision<0 : C.ExpectedWorldRevision!=-1))
        return Reject(TEXT("World revision does not match command domain"));
    const auto GuidField=[&](uint32 Bit,const FGuid& G){return (Mask&Bit)!=0 ? G.IsValid() : G==FGuid();};
    if(!GuidField(Item,C.ItemInstanceId)||!GuidField(Other,C.OtherInstanceId)||
        ((Mask&Other)!=0 && C.ItemInstanceId==C.OtherInstanceId))return Reject(TEXT("Invalid or unexpected instance identity"));
    const auto IdField=[&](uint32 Bit,const FString& S){return (Mask&Bit)!=0 ? IsId(S) : S.IsEmpty();};
    if(!IdField(Target,C.TargetStableId)||!IdField(Container,C.ContainerId)||!IdField(Definition,C.DefinitionId)||
        !IdField(Skill,C.SkillId)||!IdField(Slot,C.SlotId)||!IdField(Action,C.ActionId))
        return Reject(TEXT("Invalid or unexpected definition/target field"));
    if((Mask&Quantity)!=0 ? (C.Quantity<1||C.Quantity>1000) : C.Quantity!=0)
        return Reject(TEXT("Invalid or unexpected quantity"));
    if((Mask&Index)!=0 ? ((C.DestinationIndex<0 && !(C.Type==EAetherCommandType::SplitStack && C.DestinationIndex==-1))||C.DestinationIndex>=256) : C.DestinationIndex!=-1)
        return Reject(TEXT("Invalid or unexpected destination index"));
    if((Mask&Enabled)==0 && C.Enabled)return Reject(TEXT("Unexpected flag"));
    if(uint8(C.TransferDirection)>1 || (C.Type!=EAetherCommandType::TransferItem && C.TransferDirection!=EAetherTransferDirection::IntoContainer))
        return Reject(TEXT("Invalid or unexpected transfer direction"));
    Reason.Reset();return true;
}

bool AetherCommands::Encode(const FAetherPlayerCommand& C, TArray<uint8>& Bytes, FString& Reason)
{
    Bytes.Reset();
    if(!Validate(C,Reason))return false;
    FWireWriter W;
    W.UInt(0x4143,2);W.UInt(C.ProtocolVersion,2);W.UInt(uint8(C.Type),1);W.Guid(C.CommandId);
    W.UInt(uint64(C.ExpectedProfileRevision),8);
    // -1 用全 1 表示，解码显式识别，避免无符号到有符号的溢出转换。
    W.UInt(C.ExpectedWorldRevision<0?MAX_uint64:uint64(C.ExpectedWorldRevision),8);
    W.Guid(C.ItemInstanceId);W.Guid(C.OtherInstanceId);
    W.Id(C.TargetStableId);W.Id(C.ContainerId);W.Id(C.DefinitionId);W.Id(C.SkillId);W.Id(C.SlotId);W.Id(C.ActionId);
    W.UInt(uint32(C.Quantity),4);W.UInt(uint32(C.DestinationIndex),4);W.UInt(C.Enabled?1:0,1);W.UInt(uint8(C.TransferDirection),1);
    Bytes=MoveTemp(W.Bytes);return true;
}

bool AetherCommands::Decode(const TArray<uint8>& Bytes, FAetherPlayerCommand& Out, FString& Reason)
{
    Out=FAetherPlayerCommand();
    if(Bytes.IsEmpty() || Bytes.Num()>MaxWireBytes){Reason=TEXT("Command wire size exceeds bounds");return false;}
    FWireReader R{Bytes};FAetherPlayerCommand C;
    if(R.UInt(2)!=0x4143){Reason=TEXT("Invalid command wire signature");return false;}
    C.ProtocolVersion=uint16(R.UInt(2));C.Type=EAetherCommandType(R.UInt(1));C.CommandId=R.Guid();
    const uint64 Profile=R.UInt(8),World=R.UInt(8);
    if(Profile>=uint64(MAX_int64)||(World!=MAX_uint64 && World>=uint64(MAX_int64)))
    {Reason=TEXT("Revision overflows supported range");return false;}
    C.ExpectedProfileRevision=int64(Profile);C.ExpectedWorldRevision=World==MAX_uint64?-1:int64(World);
    C.ItemInstanceId=R.Guid();C.OtherInstanceId=R.Guid();
    C.TargetStableId=R.Id();C.ContainerId=R.Id();C.DefinitionId=R.Id();C.SkillId=R.Id();C.SlotId=R.Id();C.ActionId=R.Id();
    const uint64 Quantity=R.UInt(4),Index=R.UInt(4),Enabled=R.UInt(1),Direction=R.UInt(1);
    if(Quantity>1000 || (Index!=MAX_uint32 && Index>=256) || Enabled>1 || Direction>1)
    {Reason=TEXT("Scalar payload outside bounds");return false;}
    C.Quantity=int32(Quantity);C.DestinationIndex=Index==MAX_uint32?-1:int32(Index);C.Enabled=Enabled!=0;C.TransferDirection=EAetherTransferDirection(Direction);
    if(!R.Valid || R.Offset!=Bytes.Num()){Reason=TEXT("Truncated or trailing command bytes");return false;}
    if(!Validate(C,Reason))return false;
    Out=MoveTemp(C);return true;
}

bool AetherCommands::ValidateResult(const FAetherCommandResult& R, FString& Reason)
{
    const auto Reject=[&](const TCHAR* Why){Reason=Why;return false;};
    if(!R.CommandId.IsValid() || uint8(R.Code)>uint8(EAetherCommandCode::Busy) ||
        R.FinalProfileRevision < -1 || R.FinalWorldRevision < -1 || R.ActualQuantity<0 || R.ActualQuantity>1000000)
        return Reject(TEXT("Invalid command result header"));
    const bool Committed=R.Code==EAetherCommandCode::Applied || R.Code==EAetherCommandCode::Replayed;
    if((Committed && R.FinalProfileRevision<0) || (!Committed && R.ActualQuantity!=0))
        return Reject(TEXT("Uncommitted result cannot claim a transfer"));
    if(R.AffectedIds.Num()>512 || R.Transfers.Num()>512 || R.AffectedDefinitionIds.Num()>32 || R.ReasonParameters.Num()>8)
        return Reject(TEXT("Command result exceeds bounds"));
    TSet<FGuid> Guids;TSet<FString> Ids;
    for(const auto& Id:R.AffectedIds)
    {if(!Id.IsValid()||Guids.Contains(Id))return Reject(TEXT("Invalid affected instance"));Guids.Add(Id);}
    for(const auto& Transfer:R.Transfers)
        if(!Committed||!Transfer.From.IsValid()||!Transfer.To.IsValid()||Transfer.Quantity<1||Transfer.Quantity>1000||
            !Guids.Contains(Transfer.From)||!Guids.Contains(Transfer.To))return Reject(TEXT("Invalid committed transfer record"));
    for(const auto& Id:R.AffectedDefinitionIds)
    {if(!IsId(Id)||Ids.Contains(Id))return Reject(TEXT("Invalid affected definition"));Ids.Add(Id);}
    for(const auto& Pair:R.ReasonParameters)
    {
        if(!IsId(Pair.Key)||Pair.Value.Len()>128)return Reject(TEXT("Invalid reason parameter"));
        for(TCHAR C:Pair.Value)if(C<32)return Reject(TEXT("Control characters in reason parameter"));
    }
    Reason.Reset();return true;
}

bool AetherCommands::EncodeResult(const FAetherCommandResult& R,TArray<uint8>& Bytes,FString& Reason)
{
    Bytes.Reset();if(!ValidateResult(R,Reason))return false;
    FWireWriter W;W.UInt(0x4152,2);W.UInt(ResultSchemaVersion,2);W.Guid(R.CommandId);W.UInt(uint8(R.Code),1);
    W.UInt(R.FinalProfileRevision<0?MAX_uint64:uint64(R.FinalProfileRevision),8);
    W.UInt(R.FinalWorldRevision<0?MAX_uint64:uint64(R.FinalWorldRevision),8);W.UInt(uint32(R.ActualQuantity),4);
    W.UInt(R.AffectedIds.Num(),2);for(const auto& Id:R.AffectedIds)W.Guid(Id);
    W.UInt(R.AffectedDefinitionIds.Num(),1);for(const auto& Id:R.AffectedDefinitionIds)W.Id(Id);
    // TMap 的遍历顺序不是协议；键排序使重启后的回执字节仍然一致。
    TArray<FString> Keys;R.ReasonParameters.GenerateKeyArray(Keys);
    Keys.Sort([](const FString& A,const FString& B){return A.Compare(B,ESearchCase::CaseSensitive)<0;});
    W.UInt(Keys.Num(),1);for(const auto& Key:Keys){W.Id(Key);W.Text(R.ReasonParameters.FindChecked(Key));}
    W.UInt(R.Transfers.Num(),2);for(const auto& T:R.Transfers){W.Guid(T.From);W.Guid(T.To);W.UInt(T.Quantity,4);}
    if(W.Bytes.Num()>16384){Reason=TEXT("Encoded result exceeds total wire budget");return false;}
    Bytes=MoveTemp(W.Bytes);return true;
}

bool AetherCommands::DecodeResult(const TArray<uint8>& Bytes,FAetherCommandResult& Out,FString& Reason)
{
    Out=FAetherCommandResult();
    const auto Reject=[&](const TCHAR* Why){Reason=Why;return false;};
    if(Bytes.IsEmpty()||Bytes.Num()>16384)return Reject(TEXT("Result wire size exceeds bounds"));
    FWireReader W{Bytes};FAetherCommandResult R;
    if(W.UInt(2)!=0x4152)return Reject(TEXT("Unknown result magic"));
    const uint64 Format=W.UInt(2);
    if(Format!=1&&Format!=ResultSchemaVersion)return Reject(TEXT("Unknown result format"));
    R.CommandId=W.Guid();R.Code=EAetherCommandCode(W.UInt(1));
    const uint64 Profile=W.UInt(8),World=W.UInt(8),Quantity=W.UInt(4);
    if((Profile!=MAX_uint64&&Profile>uint64(MAX_int64))||(World!=MAX_uint64&&World>uint64(MAX_int64))||Quantity>(Format==1?1000:1000000))
        return Reject(TEXT("Result scalar overflow"));
    R.FinalProfileRevision=Profile==MAX_uint64?-1:int64(Profile);
    R.FinalWorldRevision=World==MAX_uint64?-1:int64(World);R.ActualQuantity=int32(Quantity);
    int32 N=int32(W.UInt(Format==1?1:2));if(N>(Format==1?32:512))return Reject(TEXT("Too many affected instances"));
    for(int32 I=0;I<N;++I)R.AffectedIds.Add(W.Guid());
    N=int32(W.UInt(1));if(N>32)return Reject(TEXT("Too many affected definitions"));
    for(int32 I=0;I<N;++I)R.AffectedDefinitionIds.Add(W.Id());
    N=int32(W.UInt(1));if(N>8)return Reject(TEXT("Too many reason parameters"));
    for(int32 I=0;I<N;++I)
    {
        FString Key=W.Id(),Value=W.Text();
        if(R.ReasonParameters.Contains(Key))return Reject(TEXT("Duplicate reason parameter"));
        R.ReasonParameters.Add(MoveTemp(Key),MoveTemp(Value));
    }
    if(Format>=2)
    {
        N=int32(W.UInt(2));if(N>512)return Reject(TEXT("Too many transfer records"));
        for(int32 I=0;I<N&&W.Valid;++I)
        {
            FAetherCommandTransfer T;T.From=W.Guid();T.To=W.Guid();const uint64 QuantityMoved=W.UInt(4);
            if(QuantityMoved>1000)return Reject(TEXT("Transfer quantity overflow"));T.Quantity=int32(QuantityMoved);R.Transfers.Add(T);
        }
    }
    if(!W.Valid||W.Offset!=Bytes.Num())return Reject(TEXT("Truncated, invalid UTF8 or trailing result bytes"));
    if(!ValidateResult(R,Reason))return false;
    Out=MoveTemp(R);return true;
}
