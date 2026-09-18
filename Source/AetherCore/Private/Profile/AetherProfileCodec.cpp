#include "Profile/AetherProfileCodec.h"
#include "Inventory/AetherInventoryCodec.h"
#include "Skills/AetherSkillCodec.h"
namespace
{
struct FWriter
{
    TArray<uint8> Bytes;bool Valid=true;
    void UInt(uint64 V,int32 N){for(int32 I=0;I<N;++I)Bytes.Add(uint8(V>>(I*8)));}
    void Guid(const FGuid& G){UInt(G.A,4);UInt(G.B,4);UInt(G.C,4);UInt(G.D,4);}
    void Text(const FString& S)
    {
        FTCHARToUTF8 UTF8(*S);FUTF8ToTCHAR Back(UTF8.Get(),UTF8.Length());
        if(UTF8.Length()>512||FString(Back.Length(),Back.Get())!=S){Valid=false;return;}
        UInt(UTF8.Length(),2);Bytes.Append(reinterpret_cast<const uint8*>(UTF8.Get()),UTF8.Length());
    }
    void Strings(const TArray<FString>& V){UInt(V.Num(),2);for(const auto& S:V)Text(S);}
    void Blob(const TArray<uint8>& V){UInt(V.Num(),4);Bytes.Append(V);}
};
struct FReader
{
    const TArray<uint8>& Bytes;int32 Offset=0;bool Valid=true;
    uint64 UInt(int32 N)
    {
        if(!Valid||N>Bytes.Num()-Offset){Valid=false;return 0;}
        uint64 V=0;for(int32 I=0;I<N;++I)V|=uint64(Bytes[Offset++])<<(I*8);return V;
    }
    int32 Int32(){const uint32 V=uint32(UInt(4));return V<=MAX_int32?int32(V):-1-int32(MAX_uint32-V);}
    FGuid Guid(){const uint32 A=uint32(UInt(4)),B=uint32(UInt(4)),C=uint32(UInt(4)),D=uint32(UInt(4));return FGuid(A,B,C,D);}
    FString Text(int32 MaxChars)
    {
        const int32 N=int32(UInt(2));
        if(!Valid||N>512||N>Bytes.Num()-Offset){Valid=false;return {};}
        FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()+Offset),N);
        FString S(Decoded.Length(),Decoded.Get());FTCHARToUTF8 Check(*S);
        if(S.Len()>MaxChars||Check.Length()!=N||FMemory::Memcmp(Check.Get(),Bytes.GetData()+Offset,N)!=0)Valid=false;
        Offset+=N;return S;
    }
    TArray<FString> Strings(int32 Limit)
    {
        TArray<FString> Out;const int32 N=int32(UInt(2));if(!Valid||N>Limit){Valid=false;return Out;}
        for(int32 I=0;I<N&&Valid;++I)Out.Add(Text(96));return Out;
    }
    TArray<uint8> Blob(int32 Limit)
    {
        const uint64 N=UInt(4);TArray<uint8> Out;
        if(!Valid||N>uint64(Limit)||N>uint64(Bytes.Num()-Offset)){Valid=false;return Out;}
        Out.Append(Bytes.GetData()+Offset,int32(N));Offset+=int32(N);return Out;
    }
};
bool GuidLess(const FGuid& A,const FGuid& B)
{
    if(A.A!=B.A)return A.A<B.A;if(A.B!=B.B)return A.B<B.B;if(A.C!=B.C)return A.C<B.C;return A.D<B.D;
}
}
bool AetherProfileCodec::Encode(const FAetherProfileStateV10& S,const FAetherV10ItemDefinitions& Items,
    const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,TArray<uint8>& Bytes,FString& Reason)
{
    Bytes.Reset();if(!S.Validate(Items,Skills,Rules,Reason))return false;
    TArray<uint8> Inventory,SkillState;
    if(!AetherInventoryCodec::Encode(S.Inventory,Items,Inventory,Reason)||!AetherSkillCodec::Encode(S.Skills,Skills,SkillState,Reason))return false;
    FWriter W;W.UInt(0x46525041,4);W.UInt(SchemaVersion,2);W.UInt(Items.ContentSchemaVersion,2);
    W.Text(S.CharacterId);W.UInt(S.Revision,8);W.UInt(S.Gold,4);W.UInt(S.Experience,4);
    W.UInt((S.bRegistered?1:0)|(S.bCompanion?2:0),1);W.Guid(S.LastAbbeyReceipt);W.Guid(S.LastRelayReceipt);
    W.Blob(Inventory);W.Blob(SkillState);
    W.Strings(S.Evidence);W.Strings(S.Claims);W.Text(S.DailyDate);W.Strings(S.DailyEvidence);W.Strings(S.DailyClaims);
    auto Pending=S.PendingRewards;Pending.Sort([](const auto& A,const auto& B){return GuidLess(A.RewardId,B.RewardId);});
    W.UInt(Pending.Num(),2);
    for(const auto& P:Pending)
    {
        W.Guid(P.RewardId);W.Text(P.SourceId);W.UInt(P.Gold,4);
        TArray<FString> Keys;P.Items.GetKeys(Keys);Keys.Sort([](const auto& A,const auto& B){return A.Compare(B,ESearchCase::CaseSensitive)<0;});
        W.UInt(Keys.Num(),2);for(const auto& K:Keys){W.Text(K);W.UInt(P.Items[K],4);}
    }
    auto Claimed=S.ClaimedRewardIds.Array();Claimed.Sort(GuidLess);W.UInt(Claimed.Num(),2);for(const auto& G:Claimed)W.Guid(G);
    W.UInt(S.LegacySaveSchema,1);W.UInt(S.LegacyProfileRevision,4);W.Text(S.LegacySourceSha256);
    W.UInt(S.LegacyInventoryReceipts.Num(),1);
    for(const auto& P:S.LegacyInventoryReceipts)
    {
        W.Guid(P.CommandId);W.Guid(P.ItemInstanceId);W.Guid(P.DestinationInstanceId);
        W.UInt(P.ExpectedRevision,4);W.UInt(P.FinalRevision,4);W.UInt(uint32(P.Quantity),4);W.UInt(P.Transferred,4);
        W.Text(P.Action);W.Text(P.DefinitionId);W.Text(P.ShopId);
    }
    if(!W.Valid||W.Bytes.Num()>MaxBytes){Reason=TEXT("Profile cannot be encoded losslessly within size limit");return false;}
    Bytes=MoveTemp(W.Bytes);return true;
}
bool AetherProfileCodec::Decode(const TArray<uint8>& Bytes,const FAetherV10ItemDefinitions& Items,
    const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,FAetherProfileStateV10& Out,FString& Reason)
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(Bytes.Num()<16||Bytes.Num()>MaxBytes)return Fail(TEXT("Profile DTO size outside bounds"));
    FReader R{Bytes};FAetherProfileStateV10 S;
    if(R.UInt(4)!=0x46525041||R.UInt(2)!=SchemaVersion||R.UInt(2)!=uint64(Items.ContentSchemaVersion))return Fail(TEXT("Unknown profile DTO/content schema"));
    S.CharacterId=R.Text(32);const uint64 Version=R.UInt(8);if(Version>=uint64(MAX_int64))return Fail(TEXT("Invalid profile version"));
    S.Revision=int64(Version);S.Gold=R.Int32();S.Experience=R.Int32();
    const uint64 Flags=R.UInt(1);if(Flags>3)return Fail(TEXT("Unknown profile flags"));
    S.bRegistered=(Flags&1)!=0;S.bCompanion=(Flags&2)!=0;S.LastAbbeyReceipt=R.Guid();S.LastRelayReceipt=R.Guid();
    const auto Inventory=R.Blob(AetherInventoryCodec::MaxBytes),SkillState=R.Blob(AetherSkillCodec::MaxBytes);
    if(!R.Valid)return Fail(TEXT("Truncated nested profile DTO"));
    if(!AetherInventoryCodec::Decode(Inventory,Items,S.Inventory,Reason)||!AetherSkillCodec::Decode(SkillState,Skills,S.Skills,Reason))return false;
    S.Evidence=R.Strings(512);S.Claims=R.Strings(512);S.DailyDate=R.Text(8);S.DailyEvidence=R.Strings(64);S.DailyClaims=R.Strings(16);
    int32 N=int32(R.UInt(2));if(N>128)return Fail(TEXT("Too many pending rewards"));
    for(int32 I=0;I<N;++I)
    {
        FAetherPendingRewardV10 P;P.RewardId=R.Guid();P.SourceId=R.Text(96);P.Gold=R.Int32();
        const int32 Count=int32(R.UInt(2));if(Count>32)return Fail(TEXT("Too many reward item types"));
        for(int32 J=0;J<Count;++J)
        {
            const auto Key=R.Text(96);const int32 Quantity=R.Int32();
            if(!R.Valid||P.Items.Contains(Key))return Fail(TEXT("Invalid/duplicate reward item"));P.Items.Add(Key,Quantity);
        }
        if(!R.Valid)return Fail(TEXT("Truncated pending reward"));
        S.PendingRewards.Add(MoveTemp(P));
    }
    N=int32(R.UInt(2));if(N>4096)return Fail(TEXT("Too many claimed reward IDs"));
    for(int32 I=0;I<N;++I)
    {
        const auto G=R.Guid();if(!R.Valid||S.ClaimedRewardIds.Contains(G))return Fail(TEXT("Invalid/duplicate claimed reward ID"));S.ClaimedRewardIds.Add(G);
    }
    S.LegacySaveSchema=int32(R.UInt(1));S.LegacyProfileRevision=R.Int32();S.LegacySourceSha256=R.Text(64);
    N=int32(R.UInt(1));if(N>64)return Fail(TEXT("Too many frozen legacy receipts"));
    for(int32 I=0;I<N;++I)
    {
        FAetherLegacyInventoryReceiptV9 P;P.CommandId=R.Guid();P.ItemInstanceId=R.Guid();P.DestinationInstanceId=R.Guid();
        P.ExpectedRevision=R.Int32();P.FinalRevision=R.Int32();P.Quantity=R.Int32();P.Transferred=R.Int32();
        P.Action=R.Text(96);P.DefinitionId=R.Text(96);P.ShopId=R.Text(96);
        if(!R.Valid)return Fail(TEXT("Truncated frozen legacy receipt"));S.LegacyInventoryReceipts.Add(MoveTemp(P));
    }
    if(!R.Valid||R.Offset!=Bytes.Num())return Fail(TEXT("Truncated or trailing profile DTO"));
    if(!S.Validate(Items,Skills,Rules,Reason))return false;
    Out=MoveTemp(S);return true;
}
