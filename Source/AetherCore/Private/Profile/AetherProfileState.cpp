#include "Profile/AetherProfileState.h"
namespace
{
bool Id(const FString& S,bool Empty=false)
{
    if(S.IsEmpty())return Empty;if(S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))return false;
    return true;
}
bool Date(const FString& S)
{
    if(S.IsEmpty())return true;
    if(S.Len()!=8)return false;
    for(TCHAR C:S)if(C<'0'||C>'9')return false;
    const int32 Y=FCString::Atoi(*S.Left(4)),M=FCString::Atoi(*S.Mid(4,2)),D=FCString::Atoi(*S.Right(2));
    return FDateTime::Validate(Y,M,D,0,0,0,0);
}
bool Known(const TArray<FString>& Values,int32 Limit,const TSet<FString>& Catalog,bool Unique)
{
    if(Values.Num()>Limit)return false;TSet<FString> Seen;
    for(const auto& V:Values){if(!Id(V)||!Catalog.Contains(V)||(Unique&&Seen.Contains(V)))return false;Seen.Add(V);}
    return true;
}
}
bool FAetherProfileStateV10::Validate(const FAetherV10ItemDefinitions& I,const FAetherSkillDefinitionsV10& S,const FAetherRules& R,FString& Reason) const
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(!R.bValid)return Fail(TEXT("Invalid progression definitions"));
    if(!Inventory.Validate(I,Reason)||!Skills.Validate(S,Reason))return false;
    // 身份是服务器的角色键；允许旧合法 Unicode 键，格式编码会再验证无损 UTF-8。
    if(CharacterId.IsEmpty()||CharacterId.Len()>32||Revision<0||Revision==MAX_int64||
        Gold<0||Gold>10000000||Experience<0||!Date(DailyDate))return Fail(TEXT("Invalid profile identity/economy/date"));
    for(TCHAR C:CharacterId)if(C<32)return Fail(TEXT("Control character in profile identity"));
    TSet<FString> Facts,Quests,Dailies,DailyFacts;
    for(const auto& P:R.Objectives)Facts.Add(P.Key.ToString());
    for(const auto& Q:R.Quests)Quests.Add(Q.Id.ToString());
    for(const auto& D:R.Dailies){Dailies.Add(D.Id.ToString());for(const auto& F:D.Facts)DailyFacts.Add(F.ToString());}
    if(!Known(Evidence,512,Facts,false)||!Known(Claims,512,Quests,true)||
        !Known(DailyEvidence,64,DailyFacts,false)||!Known(DailyClaims,16,Dailies,true))
        return Fail(TEXT("Unknown/invalid progression ID"));
    if(PendingRewards.Num()>128||ClaimedRewardIds.Num()>4096)return Fail(TEXT("Reward ledger bounds exceeded"));
    for(const auto& G:ClaimedRewardIds)if(!G.IsValid())return Fail(TEXT("Invalid claimed reward identity"));
    TSet<FGuid> PendingIds;
    for(const auto& P:PendingRewards)
    {
        if(!P.RewardId.IsValid()||PendingIds.Contains(P.RewardId)||ClaimedRewardIds.Contains(P.RewardId)||!Id(P.SourceId)||
            P.Gold<0||P.Gold>10000000||P.Items.Num()>32||(P.Gold==0&&P.Items.IsEmpty()))return Fail(TEXT("Invalid pending reward"));
        PendingIds.Add(P.RewardId);
        for(const auto& Item:P.Items)
            if(!I.Items.Contains(Item.Key)||Item.Value<1||Item.Value>1000000)return Fail(TEXT("Unknown/invalid reward item"));
    }
    if(LegacySaveSchema==0)
    {
        if(!LegacySourceSha256.IsEmpty()||LegacyProfileRevision!=0||!LegacyInventoryReceipts.IsEmpty())
            return Fail(TEXT("Legacy metadata without migration source"));
    }
    else
    {
        if((LegacySaveSchema!=4&&LegacySaveSchema!=5)||LegacyProfileRevision<0||Revision<LegacyProfileRevision||
            LegacySourceSha256.Len()!=64||LegacyInventoryReceipts.Num()>64)return Fail(TEXT("Invalid migration source"));
        for(TCHAR C:LegacySourceSha256)if(!((C>='0'&&C<='9')||(C>='a'&&C<='f')))return Fail(TEXT("Invalid source digest"));
        TSet<FGuid> Commands;
        for(const auto& P:LegacyInventoryReceipts)
        {
            if(!P.CommandId.IsValid()||Commands.Contains(P.CommandId)||P.ExpectedRevision<0||P.ExpectedRevision==MAX_int32||
                int64(P.FinalRevision)!=int64(P.ExpectedRevision)+1||int64(P.FinalRevision)>int64(LegacyProfileRevision)+1||
                P.Transferred<1||P.Transferred>1000||!Id(P.Action)||!Id(P.DefinitionId,true)||!Id(P.ShopId,true))
                return Fail(TEXT("Invalid frozen legacy receipt"));
            Commands.Add(P.CommandId);
        }
    }
    Reason.Reset();return true;
}
