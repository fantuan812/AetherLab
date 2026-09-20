#include "Skills/AetherSkillState.h"
namespace
{
using E=EAetherSkillMutationCode;
bool Id(const FString& S)
{
    if(S.IsEmpty()||S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))return false;
    return true;
}
bool PrerequisitesMet(const FAetherSkillStateV10& S,const FAetherSkillDefinitionV10& D)
{
    // 临时装备不能充当永久学习的前置，避免卸装后留下非法的永久技能链。
    for(const auto& P:D.Prerequisites)if(S.PermanentRank(P.SkillId)<P.Rank)return false;
    return true;
}
bool Busy(const FAetherSkillRuleContext& C){return C.bInCombat||C.bCasting||C.bCoolingDown;}
}
int32 FAetherSkillStateV10::PermanentRank(const FString& SkillId) const
{return FMath::Max(LearnedRanks.FindRef(SkillId),StoryGrants.Contains(SkillId)?1:0);}
int32 FAetherSkillStateV10::EffectiveRank(const FString& SkillId,const TArray<FAetherExternalSkillGrant>& Grants) const
{
    int32 Rank=PermanentRank(SkillId);
    for(const auto& G:Grants)if(G.SkillId.Equals(SkillId,ESearchCase::CaseSensitive))Rank=FMath::Max(Rank,G.Rank);
    return Rank;
}
bool FAetherSkillStateV10::ValidateExternalGrants(const TArray<FAetherExternalSkillGrant>& Grants,const FAetherSkillDefinitionsV10& D)
{
    if(Grants.Num()>256)return false;
    TSet<FString> Keys;
    for(const auto& G:Grants)
    {
        if(!Id(G.SourceId)||!D.Effect(G.SkillId,G.Rank)||uint8(G.Source)>1)return false;
        const FString Key=FString::FromInt(int32(G.Source))+TEXT(":")+G.SourceId+TEXT(":")+G.SkillId;
        if(Keys.Contains(Key))return false;Keys.Add(Key);
    }
    return true;
}
bool FAetherSkillStateV10::Validate(const FAetherSkillDefinitionsV10& D,FString& Reason) const
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(!D.Validate(Reason))return false;
    if(LearnedRanks.Num()>D.Skills.Num()||StoryGrants.Num()>D.Skills.Num()||PointEvents.Num()>MaxPointEvents||
        Purchases.Num()>D.Skills.Num()*3||Hotbar.Num()>HotbarCapacity||AvailableSkillPoints<0||AvailableSkillPoints>MaxTotalPoints)
        return Fail(TEXT("Skill state bounds exceeded"));
    int64 Earned=0,Spent=0;
    for(const auto& P:PointEvents)
    {
        if(!Id(P.Key)||P.Value<1||P.Value>1000)return Fail(TEXT("Invalid point source event"));
        Earned+=P.Value;
    }
    if(Earned>MaxTotalPoints)return Fail(TEXT("Point ledger exceeds lifetime bound"));
    for(const auto& P:StoryGrants)
    {
        const auto* S=D.Skills.Find(P.Key);
        if(!S||!S->SkillId.Equals(P.Key,ESearchCase::CaseSensitive)||!S->bStoryBase||!Id(P.Value)||!PrerequisitesMet(*this,*S))return Fail(TEXT("Invalid story grant"));
    }
    for(const auto& P:LearnedRanks)
    {
        if(!D.Effect(P.Key,P.Value)||P.Value<=(StoryGrants.Contains(P.Key)?1:0))
            return Fail(TEXT("Invalid learned rank"));
        const auto& S=D.Skills.FindChecked(P.Key);
        if((S.bStoryBase&&!StoryGrants.Contains(P.Key))||!PrerequisitesMet(*this,S))
            return Fail(TEXT("Permanent skill lacks its story base or prerequisite"));
    }
    TSet<FGuid> Commands;TSet<FString> RankKeys;
    for(const auto& P:Purchases)
    {
        const auto* S=D.Skills.Find(P.SkillId);
        if(!S||!D.Effect(P.SkillId,P.Rank)||!P.CommandId.IsValid()||Commands.Contains(P.CommandId)||P.PaidPoints<0||P.PaidPoints>100||
            P.Rank>LearnedRanks.FindRef(P.SkillId)||P.Rank<=(StoryGrants.Contains(P.SkillId)?1:0))
            return Fail(TEXT("Invalid purchase ledger"));
        const FString Key=P.SkillId+TEXT(":")+FString::FromInt(P.Rank);
        if(RankKeys.Contains(Key))return Fail(TEXT("Duplicate rank purchase"));
        RankKeys.Add(Key);Commands.Add(P.CommandId);Spent+=P.PaidPoints;
    }
    for(const auto& P:LearnedRanks)
        for(int32 Rank=StoryGrants.Contains(P.Key)?2:1;Rank<=P.Value;++Rank)
            if(!RankKeys.Contains(P.Key+TEXT(":")+FString::FromInt(Rank)))return Fail(TEXT("Missing historical rank cost"));
    if(Earned-Spent!=AvailableSkillPoints)return Fail(TEXT("Points do not balance"));
    for(const auto& P:Hotbar)
    {
        const auto* S=D.Skills.Find(P.Value);
        // 当前失去授权仍保留可辨识快捷位；UI/施法时用 EffectiveRank 判定能否使用。
        if(P.Key<0||P.Key>=HotbarCapacity||!S||!S->SkillId.Equals(P.Value,ESearchCase::CaseSensitive)||!S->bActive)return Fail(TEXT("Invalid hotbar entry"));
    }
    Reason.Reset();return true;
}
FAetherSkillMutation FAetherSkillStateV10::Publish(FAetherSkillStateV10&& Candidate,FAetherSkillMutation Result,const FAetherSkillDefinitionsV10& D)
{
    FString Reason;if(!Candidate.Validate(D,Reason))return {E::Invalid};
    *this=MoveTemp(Candidate);return Result;
}
FAetherSkillMutation FAetherSkillStateV10::AwardPoints(const FString& EventId,int32 Points,const FAetherSkillDefinitionsV10& D)
{
    FString Reason;if(!Validate(D,Reason)||!Id(EventId)||Points<1||Points>1000)return {E::Invalid};
    if(const auto* Existing=PointEvents.Find(EventId))return {*Existing==Points?E::Unchanged:E::Conflict};
    if(PointEvents.Num()>=MaxPointEvents)return {E::Capacity};
    int64 Earned=Points;for(const auto& P:PointEvents)Earned+=P.Value;
    if(Earned>MaxTotalPoints)return {E::Capacity};
    auto Next=*this;Next.PointEvents.Add(EventId,Points);Next.AvailableSkillPoints+=Points;
    return Publish(MoveTemp(Next),{E::Applied,Points},D);
}
FAetherSkillMutation FAetherSkillStateV10::GrantStory(const FString& SkillId,const FString& EventId,const FAetherSkillDefinitionsV10& D)
{
    FString Reason;if(!Validate(D,Reason)||!Id(EventId))return {E::Invalid};
    const auto* Skill=D.Skills.Find(SkillId);if(!Skill||!Skill->SkillId.Equals(SkillId,ESearchCase::CaseSensitive))return {E::Missing};
    if(!Skill->bStoryBase)return {E::NotAuthorized};
    if(StoryGrants.Contains(SkillId))return {E::Unchanged};
    if(!PrerequisitesMet(*this,*Skill))return {E::Prerequisite};
    auto Next=*this;Next.StoryGrants.Add(SkillId,EventId);
    return Publish(MoveTemp(Next),{E::Applied,0,{SkillId}},D);
}
EAetherSkillMutationCode FAetherSkillStateV10::CanLearnNext(const FString& SkillId,const FAetherSkillRuleContext& C,const FAetherSkillDefinitionsV10& D) const
{
    FString Reason;if(!Validate(D,Reason)||C.CharacterLevel<1||C.CharacterLevel>100)return E::Invalid;
    const auto* S=D.Skills.Find(SkillId);if(!S||!S->SkillId.Equals(SkillId,ESearchCase::CaseSensitive))return E::Missing;
    if(Busy(C))return E::NotReady;
    const auto* Effect=D.Effect(SkillId,PermanentRank(SkillId)+1);
    if(!Effect)return E::MaxRank;
    if(S->bStoryBase&&!StoryGrants.Contains(SkillId))return E::StoryRequired;
    if(!PrerequisitesMet(*this,*S))return E::Prerequisite;
    if(C.CharacterLevel<Effect->RequiredLevel)return E::LevelRequired;
    if(!S->RequiredQuest.IsEmpty()&&!C.CompletedQuests.Contains(S->RequiredQuest))return E::QuestRequired;
    if(AvailableSkillPoints<Effect->PointCost)return E::InsufficientPoints;
    return E::Applied;
}
FAetherSkillMutation FAetherSkillStateV10::LearnNext(const FString& SkillId,FGuid CommandId,const FAetherSkillRuleContext& C,const FAetherSkillDefinitionsV10& D)
{
    FString Reason;if(!Validate(D,Reason)||!CommandId.IsValid()||C.CharacterLevel<1||C.CharacterLevel>100)return {E::Invalid};
    const auto* S=D.Skills.Find(SkillId);if(!S||!S->SkillId.Equals(SkillId,ESearchCase::CaseSensitive))return {E::Missing};
    if(Busy(C))return {E::NotReady};
    for(const auto& P:Purchases)if(P.CommandId==CommandId)return {E::Conflict};
    const E Allowed=CanLearnNext(SkillId,C,D);if(Allowed!=E::Applied)return {Allowed};
    const int32 Rank=PermanentRank(SkillId)+1;const auto* Effect=D.Effect(SkillId,Rank);
    auto Next=*this;Next.LearnedRanks.Add(SkillId,Rank);Next.AvailableSkillPoints-=Effect->PointCost;
    // 实付价格随记录保存。定义将来涨价/降价，退款都不会凭空增减历史成本。
    Next.Purchases.Add({SkillId,Rank,Effect->PointCost,CommandId});
    return Publish(MoveTemp(Next),{E::Applied,-Effect->PointCost,{SkillId}},D);
}
FAetherSkillMutation FAetherSkillStateV10::Reset(const FString& Root,const FAetherSkillRuleContext& C,const FAetherSkillDefinitionsV10& D,const TArray<FAetherExternalSkillGrant>& Grants)
{
    FString Reason;if(!Validate(D,Reason))return {E::Invalid};
    if(!ValidateExternalGrants(Grants,D))return {E::Invalid};
    if(!Root.IsEmpty()&&!D.Effect(Root,1))return {E::Missing};
    if(!C.bAtResetService||Busy(C))return {E::NotReady};
    auto Next=*this;
    if(Root.IsEmpty())Next.LearnedRanks.Reset();else Next.LearnedRanks.Remove(Root);
    // 每轮至少撤回一个永久学习节点，最多定义数量轮；不依赖 TMap 遍历顺序。
    bool Changed=true;
    while(Changed)
    {
        Changed=false;
        for(auto It=Next.LearnedRanks.CreateIterator();It;++It)
            if(!PrerequisitesMet(Next,D.Skills.FindChecked(It.Key()))){It.RemoveCurrent();Changed=true;}
    }
    FAetherSkillMutation Result;Result.Code=E::Unchanged;
    for(const auto& P:LearnedRanks)
        if(Next.PermanentRank(P.Key)<PermanentRank(P.Key))Result.AffectedSkills.Add(P.Key);
    if(Result.AffectedSkills.IsEmpty())return Result;
    Result.Code=E::Applied;Result.AffectedSkills.Sort();
    for(int32 I=Next.Purchases.Num()-1;I>=0;--I)
        if(Next.Purchases[I].Rank>Next.LearnedRanks.FindRef(Next.Purchases[I].SkillId))
        {Result.PointsChanged+=Next.Purchases[I].PaidPoints;Next.Purchases.RemoveAt(I);}
    Next.AvailableSkillPoints+=Result.PointsChanged;
    // 仅清理已经没有任何授权的撤回节点；装备来源不会被永久学习重置误删。
    for(auto It=Next.Hotbar.CreateIterator();It;++It)
        if(Result.AffectedSkills.Contains(It.Value())&&Next.EffectiveRank(It.Value(),Grants)==0)It.RemoveCurrent();
    return Publish(MoveTemp(Next),MoveTemp(Result),D);
}
FAetherSkillMutation FAetherSkillStateV10::Bind(int32 Slot,const FString& SkillId,const TArray<FAetherExternalSkillGrant>& Grants,const FAetherSkillDefinitionsV10& D)
{
    FString Reason;if(!Validate(D,Reason)||Slot<0||Slot>=HotbarCapacity||!ValidateExternalGrants(Grants,D))return {E::Invalid};
    const auto* Skill=D.Skills.Find(SkillId);
    if(!SkillId.IsEmpty()&&(!Skill||!Skill->SkillId.Equals(SkillId,ESearchCase::CaseSensitive)||!Skill->bActive||EffectiveRank(SkillId,Grants)<1))return {E::NotAuthorized};
    if(Hotbar.FindRef(Slot)==SkillId)return {E::Unchanged};
    auto Next=*this;if(SkillId.IsEmpty())Next.Hotbar.Remove(Slot);else Next.Hotbar.Add(Slot,SkillId);
    return Publish(MoveTemp(Next),{E::Applied},D);
}
bool FAetherSkillStateV10::FromLegacyMask(uint8 Mask,const FAetherSkillDefinitionsV10& D,FAetherSkillStateV10& Out,FString& Reason)
{
    if(Mask>15||!D.Validate(Reason)){Reason=TEXT("Invalid legacy mask/skill definitions");return false;}
    FAetherSkillStateV10 Candidate;
    for(int32 Bit=0;Bit<4;++Bit)if((Mask&(1<<Bit))!=0)
    {
        const auto* S=D.Legacy(Bit);
        if(!S||!S->bStoryBase){Reason=TEXT("Missing legacy story mapping");return false;}
        Candidate.StoryGrants.Add(S->SkillId,FString::Printf(TEXT("Legacy.V9.Spell.%d"),Bit));
        Candidate.Hotbar.Add(Bit,S->SkillId);
    }
    if(!Candidate.Validate(D,Reason))return false;
    // 历史位只能证明故事基础已学，不能虚构点数、支付或退款记录。
    Out=MoveTemp(Candidate);return true;
}
