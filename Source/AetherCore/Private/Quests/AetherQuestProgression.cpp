#include "Quests/AetherQuestProgression.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
namespace
{
const FAetherQuestRule* Quest(const FAetherRules& R,const FString& Id)
{
    const auto* Q=R.Quest(FName(*Id));return Q&&Q->Id.ToString().Equals(Id,ESearchCase::CaseSensitive)?Q:nullptr;
}
FString PointEvent(const FString& Id){return TEXT("Quest.")+Id;}
bool Award(FAetherProfileStateV10& P,const FString& Id,const FAetherQuestProgressionDefinitions& D,const FAetherSkillDefinitionsV10& Skills,int32& Points)
{
    const int32 Value=D.QuestSkillPoints.FindRef(Id);const FString Event=PointEvent(Id);
    if(Value==0||P.Skills.PointEvents.Contains(Event))return true;
    const auto Result=P.Skills.AwardPoints(Event,Value,Skills);
    if(Result.Code!=EAetherSkillMutationCode::Applied)return false;
    Points+=Value;return true;
}
bool FactsValid(const TMap<FString,FString>& Facts,const FAetherRules& Rules)
{
    if(Facts.Num()>512)return false;
    for(const auto& F:Facts)
    {
        bool Known=false;
        for(const auto& O:Rules.Objectives)if(O.Key.ToString().Equals(F.Key,ESearchCase::CaseSensitive)&&O.Value.Scope==EAetherObjectiveScope::World)
            for(const auto& Source:O.Value.FactSources)Known|=Source.ToString().Equals(F.Value,ESearchCase::CaseSensitive);
        if(!Known)return false;
    }
    return true;
}
}
bool FAetherQuestProgressionDefinitions::Validate(const FAetherRules& Rules,FString& Reason) const
{
    const auto Fail=[&](const TCHAR* S){Reason=S;return false;};
    if(!Rules.bValid||SchemaVersion!=1||QuestSkillPoints.IsEmpty()||QuestSkillPoints.Num()!=Rules.Quests.Num()||QuestSkillPoints.Num()>128)
        return Fail(TEXT("Progression must define points for every quest"));
    for(const auto& Pair:QuestSkillPoints)
    {
        if(!Quest(Rules,Pair.Key)||Pair.Key.Len()>90||Pair.Value<0||Pair.Value>1000)return Fail(TEXT("Invalid quest point source"));
        for(TCHAR C:Pair.Key)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))return Fail(TEXT("Invalid quest event ID"));
    }
    Reason.Reset();return true;
}
FAetherQuestProgressionDefinitions FAetherQuestProgressionDefinitions::Parse(const FString& Json,const FAetherRules& Rules,FString& Reason)
{
    FAetherQuestProgressionDefinitions D;TSharedPtr<FJsonObject> Root;double Version=0;const TSharedPtr<FJsonObject>* Points=nullptr;
    if(Json.Len()>128*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Root->TryGetNumberField(TEXT("SchemaVersion"),Version)||Version!=1||!Root->TryGetObjectField(TEXT("QuestSkillPoints"),Points)||(*Points)->Values.Num()>128)
    {Reason=TEXT("Invalid progression JSON/schema");return {};}
    for(const auto& P:(*Points)->Values)
    {
        double N=0;if(!P.Value->TryGetNumber(N)||!FMath::IsFinite(N)||N<0||N>1000||FMath::FloorToDouble(N)!=N||D.QuestSkillPoints.Contains(FString(*P.Key)))
        {Reason=TEXT("Invalid/duplicate quest point value");return {};}
        D.QuestSkillPoints.Add(FString(*P.Key),int32(N));
    }
    if(!D.Validate(Rules,Reason))return {};return D;
}
bool AetherQuestProgression::Available(const FAetherProfileStateV10& P,const FString& Id,const FAetherRules& R)
{
    const auto* Q=Quest(R,Id);if(!R.bValid||!Q||P.Claims.Contains(Id))return false;
    for(const auto& Required:Q->Prerequisites)if(!P.Claims.Contains(Required.ToString()))return false;return true;
}
bool AetherQuestProgression::Complete(const FAetherProfileStateV10& P,const FString& Id,const FAetherRules& R)
{
    if(!Available(P,Id,R))return false;
    for(const auto& Fact:Quest(R,Id)->Objectives)if(!P.Evidence.Contains(Fact.ToString()))return false;return true;
}
bool AetherQuestProgression::Observe(FAetherProfileStateV10& P,const FString& Fact,const FAetherRules& R)
{
    if(P.Evidence.Contains(Fact)||P.Evidence.Num()>=512)return false;
    for(const auto& Q:R.Quests)if(Available(P,Q.Id.ToString(),R))
        for(const auto& F:Q.Objectives)if(F.ToString().Equals(Fact,ESearchCase::CaseSensitive)){P.Evidence.Add(Fact);return true;}
    return false;
}
bool AetherQuestProgression::HasClaimableSkillPoints(const FAetherProfileStateV10& P,const FAetherQuestProgressionDefinitions& D)
{
    for(const auto& Id:P.Claims)if(D.QuestSkillPoints.FindRef(Id)>0&&!P.Skills.PointEvents.Contains(PointEvent(Id)))return true;
    return false;
}
FAetherQuestMutation AetherQuestProgression::ClaimSkillPoints(FAetherProfileStateV10& P,const FAetherV10ItemDefinitions& Items,
    const FAetherSkillDefinitionsV10& Skills,const FAetherRules& R,const FAetherQuestProgressionDefinitions& D)
{
    FString Reason;if(!D.Validate(R,Reason)||!P.Validate(Items,Skills,R,Reason))return {};
    auto Next=P;FAetherQuestMutation Result;Result.Code=EAetherQuestMutationCode::Unchanged;
    for(const auto& Id:P.Claims)if(!Award(Next,Id,D,Skills,Result.AwardedSkillPoints))return {EAetherQuestMutationCode::Capacity};
    if(Result.AwardedSkillPoints>0)
    {
        if(!Next.Validate(Items,Skills,R,Reason))return {};
        Result.Code=EAetherQuestMutationCode::Applied;P=MoveTemp(Next);
    }
    return Result;
}
FAetherQuestMutation AetherQuestProgression::Settle(FAetherProfileStateV10& P,const TMap<FString,FString>& Facts,const FString& Manual,
    const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,const FAetherRules& R,const FAetherQuestProgressionDefinitions& D)
{
    using E=EAetherQuestMutationCode;FString Reason;
    if(!D.Validate(R,Reason)||!P.Validate(Items,Skills,R,Reason)||!FactsValid(Facts,R)||(!Manual.IsEmpty()&&!Quest(R,Manual)))return {};
    if(!Manual.IsEmpty()&&P.Claims.Contains(Manual))return {E::NotAllowed};
    auto Next=P;FAetherQuestMutation Result;Result.Code=E::Unchanged;
    // 每次成功轮转至少完成一个新任务；最后一轮只补充新开放任务的可追溯世界事实。
    for(int32 Pass=0;Pass<=R.Quests.Num();++Pass)
    {
        bool Claimed=false;
        for(const auto& Q:R.Quests)
        {
            if(Q.Gold<0||Q.Gold>10000000||Q.Experience<0||Q.Items.Num()>32)return {};
            const FString Id=Q.Id.ToString();if(!Available(Next,Id,R))continue;
            for(const auto& F:Q.Objectives)
            {
                const auto* Rule=R.Objectives.Find(F);
                if(Rule&&Rule->Scope==EAetherObjectiveScope::World&&Rule->bRetroactive&&Facts.Contains(F.ToString()))
                    if(Observe(Next,F.ToString(),R))Result.Code=E::Applied;
            }
            if((!Q.bAutoClaim&&!Id.Equals(Manual,ESearchCase::CaseSensitive))||!Complete(Next,Id,R))continue;
            if(Next.Experience>MAX_int32-Q.Experience||Next.Claims.Num()>=512)return {E::Capacity};
            // 先试装整份奖励。任何一项不够空间，都丢弃试装副本，把整份奖励记录为 pending。
            auto Granted=Next;bool Fits=int64(Next.Gold)+Q.Gold<=10000000;
            TArray<FName> Keys;Q.Items.GetKeys(Keys);Keys.Sort([](FName A,FName B){return A.ToString().Compare(B.ToString(),ESearchCase::CaseSensitive)<0;});
            for(const auto& Key:Keys)if(Fits)
            {
                const auto Added=Granted.Inventory.AddNew(Key.ToString(),Q.Items[Key],Items);
                if(Added.Code==EAetherInventoryMutationCode::Capacity)Fits=false;
                else if(Added.Code!=EAetherInventoryMutationCode::Applied)return {};
            }
            if(Fits){Granted.Gold+=Q.Gold;Next=MoveTemp(Granted);}
            else
            {
                if(Next.PendingRewards.Num()>=128)return {E::Capacity};
                FAetherPendingRewardV10 Reward;Reward.RewardId=FGuid::NewGuid();Reward.SourceId=PointEvent(Id);Reward.Gold=Q.Gold;
                for(const auto& Key:Keys)Reward.Items.Add(Key.ToString(),Q.Items[Key]);
                Next.PendingRewards.Add(Reward);Result.DeferredRewards.Add(Reward.RewardId);
            }
            Next.Claims.Add(Id);Next.Experience+=Q.Experience;Next.bRegistered|=Q.bBindInn;
            if(!Award(Next,Id,D,Skills,Result.AwardedSkillPoints))return {E::Capacity};
            Result.ClaimedQuests.Add(Id);Result.Code=E::Applied;Claimed=true;
        }
        if(!Claimed)break;
    }
    if(!Manual.IsEmpty()&&!Next.Claims.Contains(Manual))return {E::NotAllowed};
    if(!Next.Validate(Items,Skills,R,Reason))return {};
    if(Result.Code==E::Applied)P=MoveTemp(Next);return Result;
}
