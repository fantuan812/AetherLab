#include "Definitions/AetherRules.h"
#include "Dom/JsonObject.h"
namespace
{
bool Stable(const FString& S)
{
    if(S.IsEmpty()||S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))return false;
    return true;
}
}
bool AetherReadActivityRules(FAetherRules& R,const TSharedPtr<FJsonObject>& Root)
{
    const auto Fail=[&](const TCHAR* Why){R.Error=Why;return false;};
    // 冻结 v9 规则没有这些字段，显式保留当时已写入存档的合法标记和奖励值。
    // 正常 v10 规范文件提供数据；不修改冻结夹具或把未知旧标记一律放行。
    if(!Root->HasField(TEXT("ActivityRewards")))
    {
        FAetherActivityRewardRule A;A.DailyClaim="AbbeyReward";A.Objective="GuardianDefeated";A.Gold=60;A.Items.Add("Material",3);
        R.ActivityRewards.Add("Abbey",A);A.DailyClaim="RelayReward";A.Objective=NAME_None;R.ActivityRewards.Add("Relay",A);
    }
    else
    {
        const TSharedPtr<FJsonObject>* Rewards=nullptr;
        if(!Root->TryGetObjectField(TEXT("ActivityRewards"),Rewards)||(*Rewards)->Values.Num()>16)return Fail(TEXT("Invalid activity rewards"));
        TSet<FName> Claims;
        for(const auto& Pair:(*Rewards)->Values)
        {
            const auto O=Pair.Value->AsObject();const TSharedPtr<FJsonObject>* Items=nullptr;
            FString Claim,Objective;double Gold=0;FAetherActivityRewardRule A;
            if(!Stable(Pair.Key)||R.ActivityRewards.Contains(FName(*Pair.Key))||!O||!O->TryGetStringField(TEXT("DailyClaim"),Claim)||!Stable(Claim)||Claims.Contains(FName(*Claim))||
                !O->TryGetStringField(TEXT("Objective"),Objective)||(!Objective.IsEmpty()&&!R.Objectives.Contains(FName(*Objective)))||
                !O->TryGetNumberField(TEXT("Gold"),Gold)||!FMath::IsFinite(Gold)||Gold<0||Gold>10000000||Gold!=FMath::FloorToDouble(Gold)||
                !O->TryGetObjectField(TEXT("Items"),Items)||(*Items)->Values.Num()>32)return Fail(TEXT("Invalid activity reward entry"));
            A.DailyClaim=FName(*Claim);A.Objective=FName(*Objective);A.Gold=int32(Gold);Claims.Add(A.DailyClaim);
            for(const auto& Item:(*Items)->Values)
            {
                double Count=0;if(!R.Items.Contains(FName(*Item.Key))||!Item.Value->TryGetNumber(Count)||!FMath::IsFinite(Count)||
                    Count<1||Count>1000000||Count!=FMath::FloorToDouble(Count))return Fail(TEXT("Invalid activity reward item"));
                A.Items.Add(FName(*Item.Key),int32(Count));
            }
            if(A.Gold==0&&A.Items.IsEmpty())return Fail(TEXT("Empty activity reward"));
            R.ActivityRewards.Add(FName(*Pair.Key),MoveTemp(A));
        }
    }
    if(!Root->HasField(TEXT("DailyGatherSources")))R.DailyGatherSources={"SupplyCache0","SupplyCache1","SupplyCache2"};
    else
    {
        const TArray<TSharedPtr<FJsonValue>>* Sources=nullptr;
        if(!Root->TryGetArrayField(TEXT("DailyGatherSources"),Sources)||Sources->Num()>64)return Fail(TEXT("Invalid daily gather sources"));
        for(const auto& Value:*Sources)
        {
            FString Id;if(!Value->TryGetString(Id)||!Stable(Id)||R.DailyGatherSources.Contains(FName(*Id)))return Fail(TEXT("Invalid daily gather identity"));
            R.DailyGatherSources.Add(FName(*Id));
        }
    }
    if(!R.ActivityRewards.Contains("Abbey")||!R.ActivityRewards.Contains("Relay")||R.Dailies.Num()+R.ActivityRewards.Num()>16)
        return Fail(TEXT("Missing activity reward or daily claim capacity exceeded"));
    return true;
}
