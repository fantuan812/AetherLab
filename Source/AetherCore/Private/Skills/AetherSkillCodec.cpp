#include "Skills/AetherSkillCodec.h"
namespace
{
struct FWriter
{
    TArray<uint8> Bytes;
    void UInt(uint64 V,int32 N){for(int32 I=0;I<N;++I)Bytes.Add(uint8(V>>(I*8)));}
    void Text(const FString& S)
    {
        // 状态不变量保证所有持久键为 1..96 个 ASCII 字符，不序列化本地化展示文案。
        UInt(S.Len(),1);for(TCHAR C:S)Bytes.Add(uint8(C));
    }
    void Guid(const FGuid& G){UInt(G.A,4);UInt(G.B,4);UInt(G.C,4);UInt(G.D,4);}
};
struct FReader
{
    const TArray<uint8>& Bytes;int32 Offset=0;bool Valid=true;
    uint64 UInt(int32 N)
    {
        if(!Valid||N>Bytes.Num()-Offset){Valid=false;return 0;}
        uint64 V=0;for(int32 I=0;I<N;++I)V|=uint64(Bytes[Offset++])<<(I*8);return V;
    }
    FString Text()
    {
        const int32 N=int32(UInt(1));if(!Valid||N<1||N>96||N>Bytes.Num()-Offset){Valid=false;return {};}
        FString S;S.Reserve(N);
        for(int32 I=0;I<N;++I)
        {
            const uint8 C=Bytes[Offset++];
            if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))Valid=false;
            S.AppendChar(TCHAR(C));
        }
        return S;
    }
    FGuid Guid()
    {
        const uint32 A=uint32(UInt(4)),B=uint32(UInt(4)),C=uint32(UInt(4)),D=uint32(UInt(4));return FGuid(A,B,C,D);
    }
};
template<class T> TArray<FString> Keys(const TMap<FString,T>& M)
{
    TArray<FString> K;M.GetKeys(K);
    K.Sort([](const FString& A,const FString& B){return A.Compare(B,ESearchCase::CaseSensitive)<0;});return K;
}
}
bool AetherSkillCodec::Encode(const FAetherSkillStateV10& S,const FAetherSkillDefinitionsV10& D,TArray<uint8>& Bytes,FString& Reason)
{
    Bytes.Reset();if(!S.Validate(D,Reason))return false;
    FWriter W;W.UInt(0x4C4B5341,4);W.UInt(SchemaVersion,2);W.UInt(D.ContentSchemaVersion,2);
    W.UInt(S.AvailableSkillPoints,4);
    W.UInt(S.LearnedRanks.Num(),2);for(const auto& K:Keys(S.LearnedRanks)){W.Text(K);W.UInt(S.LearnedRanks[K],1);}
    W.UInt(S.StoryGrants.Num(),2);for(const auto& K:Keys(S.StoryGrants)){W.Text(K);W.Text(S.StoryGrants[K]);}
    W.UInt(S.PointEvents.Num(),2);for(const auto& K:Keys(S.PointEvents)){W.Text(K);W.UInt(S.PointEvents[K],2);}
    auto Purchases=S.Purchases;
    Purchases.Sort([](const auto& A,const auto& B){const int32 Order=A.SkillId.Compare(B.SkillId,ESearchCase::CaseSensitive);return Order<0||(Order==0&&A.Rank<B.Rank);});
    W.UInt(Purchases.Num(),2);
    for(const auto& P:Purchases){W.Text(P.SkillId);W.UInt(P.Rank,1);W.UInt(P.PaidPoints,1);W.Guid(P.CommandId);}
    TArray<int32> Slots;S.Hotbar.GetKeys(Slots);Slots.Sort();
    W.UInt(Slots.Num(),1);for(int32 Slot:Slots){W.UInt(Slot,1);W.Text(S.Hotbar[Slot]);}
    if(W.Bytes.Num()>MaxBytes){Reason=TEXT("Skill DTO exceeds byte limit");return false;}
    Bytes=MoveTemp(W.Bytes);return true;
}
bool AetherSkillCodec::Decode(const TArray<uint8>& Bytes,const FAetherSkillDefinitionsV10& D,FAetherSkillStateV10& Out,FString& Reason)
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(Bytes.Num()<21||Bytes.Num()>MaxBytes)return Fail(TEXT("Skill DTO size outside bounds"));
    if(!D.Validate(Reason))return false;
    FReader R{Bytes};FAetherSkillStateV10 S;
    if(R.UInt(4)!=0x4C4B5341||R.UInt(2)!=SchemaVersion||R.UInt(2)!=uint64(D.ContentSchemaVersion))return Fail(TEXT("Unknown skill DTO/content schema"));
    const auto Available=R.UInt(4);if(Available>FAetherSkillStateV10::MaxTotalPoints)return Fail(TEXT("Skill point count outside bounds"));
    S.AvailableSkillPoints=int32(Available);
    int32 N=int32(R.UInt(2));if(N>D.Skills.Num())return Fail(TEXT("Too many learned skills"));
    for(int32 I=0;I<N;++I)
    {
        const auto K=R.Text();const int32 Rank=int32(R.UInt(1));
        if(!R.Valid||S.LearnedRanks.Contains(K))return Fail(TEXT("Invalid/duplicate learned skill"));
        S.LearnedRanks.Add(K,Rank);
    }
    N=int32(R.UInt(2));if(N>D.Skills.Num())return Fail(TEXT("Too many story grants"));
    for(int32 I=0;I<N;++I)
    {
        const auto K=R.Text(),Event=R.Text();
        if(!R.Valid||S.StoryGrants.Contains(K))return Fail(TEXT("Invalid/duplicate story grant"));
        S.StoryGrants.Add(K,Event);
    }
    N=int32(R.UInt(2));if(N>FAetherSkillStateV10::MaxPointEvents)return Fail(TEXT("Too many point events"));
    for(int32 I=0;I<N;++I)
    {
        const auto K=R.Text();const int32 Points=int32(R.UInt(2));
        if(!R.Valid||S.PointEvents.Contains(K))return Fail(TEXT("Invalid/duplicate point event"));
        S.PointEvents.Add(K,Points);
    }
    N=int32(R.UInt(2));if(N>D.Skills.Num()*3)return Fail(TEXT("Too many purchase records"));
    for(int32 I=0;I<N;++I)
    {
        FAetherSkillPurchase P;P.SkillId=R.Text();P.Rank=int32(R.UInt(1));P.PaidPoints=int32(R.UInt(1));P.CommandId=R.Guid();
        if(!R.Valid)return Fail(TEXT("Invalid purchase record"));
        S.Purchases.Add(MoveTemp(P));
    }
    N=int32(R.UInt(1));if(N>FAetherSkillStateV10::HotbarCapacity)return Fail(TEXT("Too many hotbar entries"));
    for(int32 I=0;I<N;++I)
    {
        const int32 Slot=int32(R.UInt(1));const auto K=R.Text();
        if(!R.Valid||S.Hotbar.Contains(Slot))return Fail(TEXT("Invalid/duplicate hotbar entry"));
        S.Hotbar.Add(Slot,K);
    }
    if(!R.Valid||R.Offset!=Bytes.Num())return Fail(TEXT("Truncated or trailing skill DTO"));
    if(!S.Validate(D,Reason))return false;
    Out=MoveTemp(S);return true;
}
