#include "Inventory/AetherInventoryCodec.h"
namespace
{
struct FWriter
{
    TArray<uint8> Bytes;
    bool Valid=true;
    void UInt(uint64 V,int32 N){for(int32 I=0;I<N;++I)Bytes.Add(uint8(V>>(I*8)));}
    void Guid(const FGuid& G){UInt(G.A,4);UInt(G.B,4);UInt(G.C,4);UInt(G.D,4);}
    void Text(const FString& S)
    {
        FTCHARToUTF8 Encoded(*S);FUTF8ToTCHAR Check(Encoded.Get(),Encoded.Length());
        // 无效 UTF-16（如孤立代理项）不能经过替换字符静默改变存档身份。
        if(Encoded.Length()>512||FString(Check.Length(),Check.Get())!=S){Valid=false;return;}
        UInt(Encoded.Length(),2);Bytes.Append(reinterpret_cast<const uint8*>(Encoded.Get()),Encoded.Length());
    }
};
struct FReader
{
    const TArray<uint8>& Bytes;
    int32 Offset=0;
    bool Valid=true;
    uint64 UInt(int32 N)
    {
        if(!Valid||N>Bytes.Num()-Offset){Valid=false;return 0;}
        uint64 V=0;for(int32 I=0;I<N;++I)V|=uint64(Bytes[Offset++])<<(I*8);return V;
    }
    FGuid Guid()
    {
        const uint32 A=uint32(UInt(4)),B=uint32(UInt(4)),C=uint32(UInt(4)),D=uint32(UInt(4));
        return FGuid(A,B,C,D);
    }
    FString Text(int32 MaxCharacters)
    {
        const int32 N=int32(UInt(2));
        if(!Valid||N>512||N>Bytes.Num()-Offset){Valid=false;return {};}
        FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()+Offset),N);
        FString S(Decoded.Length(),Decoded.Get());FTCHARToUTF8 Encoded(*S);
        if(S.Len()>MaxCharacters||Encoded.Length()!=N||FMemory::Memcmp(Encoded.Get(),Bytes.GetData()+Offset,N)!=0)Valid=false;
        Offset+=N;return S;
    }
};
TArray<FString> SortedKeys(const TMap<FString,FGuid>& Map)
{
    TArray<FString> Keys;Map.GenerateKeyArray(Keys);
    Keys.Sort([](const FString& A,const FString& B){return A.Compare(B,ESearchCase::CaseSensitive)<0;});return Keys;
}
}

bool AetherInventoryCodec::Encode(const FAetherInventoryStateV10& S,const FAetherV10ItemDefinitions& D,TArray<uint8>& Bytes,FString& Reason)
{
    Bytes.Reset();if(!S.Validate(D,Reason))return false;
    FWriter W;
    W.UInt(0x564E4941,4);W.UInt(SchemaVersion,2);W.UInt(D.ContentSchemaVersion,2);
    W.UInt(S.Capacity,2);W.UInt(S.Items.Num(),2);
    // 写入顺序只由真实格子决定，容器内部数组和 map 的插入顺序不属于格式。
    TArray<const FAetherV10ItemInstance*> Items;for(const auto& I:S.Items)Items.Add(&I);
    Items.Sort([](const FAetherV10ItemInstance& A,const FAetherV10ItemInstance& B){return A.SlotIndex<B.SlotIndex;});
    for(const auto* I:Items)
    {
        W.Guid(I->InstanceId);W.Text(I->DefinitionId);W.UInt(I->Quantity,2);W.UInt(I->SlotIndex,2);W.UInt(I->Quality,1);
        W.UInt(I->Durability<0?MAX_uint32:uint32(I->Durability),4);
        TArray<FString> Affixes;I->Affixes.GenerateKeyArray(Affixes);
        Affixes.Sort([](const FString& A,const FString& B){return A.Compare(B,ESearchCase::CaseSensitive)<0;});
        W.UInt(Affixes.Num(),1);for(const auto& Key:Affixes){W.Text(Key);W.UInt(I->Affixes.FindChecked(Key),4);}
        W.Text(I->BoundToCharacter);W.Text(I->StateGroup);W.Guid(I->QuestInstanceId);
        W.UInt((I->bLocked?1:0)|(I->bFavorite?2:0),1);
    }
    W.UInt(S.Equipment.Num(),1);
    for(const auto& Slot:SortedKeys(S.Equipment)){W.Text(Slot);W.Guid(S.Equipment.FindChecked(Slot));}
    if(!W.Valid||W.Bytes.Num()>MaxBytes){Reason=TEXT("Inventory text/size cannot be encoded losslessly");return false;}
    Bytes=MoveTemp(W.Bytes);return true;
}

bool AetherInventoryCodec::Decode(const TArray<uint8>& Bytes,const FAetherV10ItemDefinitions& D,FAetherInventoryStateV10& Out,FString& Reason)
{
    const auto Reject=[&](const TCHAR* Text){Reason=Text;return false;};
    if(Bytes.Num()<13||Bytes.Num()>MaxBytes)return Reject(TEXT("Inventory payload size outside bounds"));
    if(!D.Validate(Reason))return false;
    FReader R{Bytes};FAetherInventoryStateV10 Candidate;
    if(R.UInt(4)!=0x564E4941||R.UInt(2)!=SchemaVersion||R.UInt(2)!=uint64(D.ContentSchemaVersion))
        return Reject(TEXT("Unsupported inventory DTO or content schema"));
    Candidate.Capacity=int32(R.UInt(2));const int32 Count=int32(R.UInt(2));
    if(Candidate.Capacity!=D.DefaultCapacity||Count>Candidate.Capacity)return Reject(TEXT("Invalid persisted inventory capacity/count"));
    for(int32 N=0;N<Count;++N)
    {
        FAetherV10ItemInstance I;I.InstanceId=R.Guid();I.DefinitionId=R.Text(96);
        I.Quantity=int32(R.UInt(2));I.SlotIndex=int32(R.UInt(2));I.Quality=int32(R.UInt(1));
        const uint64 Durability=R.UInt(4);
        if(Durability!=MAX_uint32&&Durability>1000000)return Reject(TEXT("Invalid durability field"));
        I.Durability=Durability==MAX_uint32?-1:int32(Durability);
        const int32 Affixes=int32(R.UInt(1));if(Affixes>16)return Reject(TEXT("Too many persisted affixes"));
        for(int32 A=0;A<Affixes;++A)
        {
            FString Key=R.Text(96);const uint64 Value=R.UInt(4);
            if(Value>1000000||I.Affixes.Contains(Key))return Reject(TEXT("Invalid or duplicate affix"));
            I.Affixes.Add(MoveTemp(Key),int32(Value));
        }
        I.BoundToCharacter=R.Text(128);I.StateGroup=R.Text(96);I.QuestInstanceId=R.Guid();
        const uint64 Flags=R.UInt(1);if(Flags>3)return Reject(TEXT("Unknown item state flags"));
        I.bLocked=(Flags&1)!=0;I.bFavorite=(Flags&2)!=0;
        if(!R.Valid)return Reject(TEXT("Truncated or invalid inventory text"));
        Candidate.Items.Add(MoveTemp(I));
    }
    const int32 Equipped=int32(R.UInt(1));if(Equipped>D.Slots.Num())return Reject(TEXT("Too many persisted equipment references"));
    for(int32 N=0;N<Equipped;++N)
    {
        FString Slot=R.Text(96);const FGuid Id=R.Guid();
        if(Candidate.Equipment.Contains(Slot))return Reject(TEXT("Duplicate persisted equipment slot"));
        Candidate.Equipment.Add(MoveTemp(Slot),Id);
    }
    if(!R.Valid||R.Offset!=Bytes.Num())return Reject(TEXT("Truncated or trailing inventory bytes"));
    if(!Candidate.Validate(D,Reason))return false;
    // 此处是唯一发布点；上面任何错误都不能把调用方的有效库存清空。
    Out=MoveTemp(Candidate);return true;
}
