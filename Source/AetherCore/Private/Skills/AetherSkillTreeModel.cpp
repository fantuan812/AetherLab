#include "Skills/AetherSkillTreeModel.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

bool FAetherSkillTreeLayout::Validate(const FAetherSkillDefinitionsV10& D,FString& Reason) const
{
    if(!D.Validate(Reason))return false;
    int32 Count=0;for(const auto& Pair:D.Skills)Count+=Pair.Value.Ranks.Num();
    if(Nodes.Num()!=Count){Reason=TEXT("Skill layout must contain each defined rank exactly once");return false;}
    for(int32 I=0;I<Nodes.Num();++I)
    {
        const auto& N=Nodes[I];
        if(!D.Effect(N.Node.SkillId,N.Node.Rank)||N.Position.ContainsNaN()||FMath::Abs(N.Position.X)>100000||FMath::Abs(N.Position.Y)>100000)
        {Reason=TEXT("Invalid skill layout identity or coordinates");return false;}
        for(int32 J=0;J<I;++J)
            if(Nodes[J].Node==N.Node||(FMath::Abs(Nodes[J].Position.X-N.Position.X)<160&&FMath::Abs(Nodes[J].Position.Y-N.Position.Y)<108))
            {Reason=TEXT("Duplicate or overlapping skill layout node");return false;}
    }
    Reason.Reset();return true;
}
FAetherSkillTreeLayout FAetherSkillTreeLayout::Parse(const FString& Json,const FAetherSkillDefinitionsV10& D,FString& Reason)
{
    FAetherSkillTreeLayout Out;TSharedPtr<FJsonObject> Root;
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;double Version=0;
    if(Json.Len()>256*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Root->TryGetNumberField(TEXT("Version"),Version)||Version!=1||!Root->TryGetArrayField(TEXT("Nodes"),Values)||Values->Num()>384)
    {Reason=TEXT("Invalid skill layout document");return {};}
    for(const auto& V:*Values)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherSkillNodePlacement N;double Rank=0,X=0,Y=0;
        if(!V->TryGetObject(O)||!O||!O->IsValid()||!(*O)->TryGetStringField(TEXT("SkillId"),N.Node.SkillId)||
            !(*O)->TryGetNumberField(TEXT("Rank"),Rank)||!FMath::IsFinite(Rank)||Rank<1||Rank>3||FMath::FloorToDouble(Rank)!=Rank||
            !(*O)->TryGetNumberField(TEXT("X"),X)||!(*O)->TryGetNumberField(TEXT("Y"),Y))
        {Reason=TEXT("Invalid skill layout node");return {};}
        N.Node.Rank=int32(Rank);N.Position=FVector2D(X,Y);Out.Nodes.Add(MoveTemp(N));
    }
    if(!Out.Validate(D,Reason))return {};return Out;
}
FAetherSkillTreeLayout FAetherSkillTreeLayout::Default(const FAetherSkillDefinitionsV10& D)
{
    FAetherSkillTreeLayout L;TArray<FString> Keys;D.Skills.GenerateKeyArray(Keys);
    Keys.Sort([&](const auto& A,const auto& B)
    {
        const auto& DA=D.Skills.FindChecked(A);const auto& DB=D.Skills.FindChecked(B);
        if(DA.Mechanic!=DB.Mechanic)return uint8(DA.Mechanic)<uint8(DB.Mechanic);
        return A.Compare(B,ESearchCase::CaseSensitive)<0;
    });
    for(int32 Row=0;Row<Keys.Num();++Row)
        for(int32 Rank=1;Rank<=D.Skills.FindChecked(Keys[Row]).Ranks.Num();++Rank)
            L.Nodes.Add({{Keys[Row],Rank},FVector2D((Rank-1)*220,Row*150)});
    return L;
}
const FAetherSkillTreeLayout& FAetherSkillTreeLayout::Get()
{
    static const auto L=[]
    {
        const auto& D=FAetherSkillDefinitionsV10::Get();FString Json,Reason;
        if(FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/SkillTreeLayout.json"))))
        {
            auto Result=Parse(Json,D,Reason);if(Reason.IsEmpty())return Result;
        }
        // 仅回退展示布局；绝不修改定义前置图或永久学习状态。
        UE_LOG(LogTemp,Warning,TEXT("Skill layout unavailable: %s; using deterministic layout"),*Reason);
        return Default(D);
    }();return L;
}
FString AetherSkillTree::StateLabel(EAetherSkillNodeState S)
{
    switch(S)
    {
    case EAetherSkillNodeState::Unopened:return TEXT("尚未开放");
    case EAetherSkillNodeState::Prerequisite:return TEXT("缺少前置");
    case EAetherSkillNodeState::Insufficient:return TEXT("点数不足");
    case EAetherSkillNodeState::Available:return TEXT("可学习");
    case EAetherSkillNodeState::Learned:return TEXT("已学习");
    case EAetherSkillNodeState::Upgradeable:return TEXT("已学 · 可升级");
    case EAetherSkillNodeState::Maximum:return TEXT("已满级");
    case EAetherSkillNodeState::Granted:return TEXT("外部授予");
    case EAetherSkillNodeState::Pending:return TEXT("处理中");
    default:return TEXT("暂不可学习");
    }
}
FAetherSkillTreeModel AetherSkillTree::Build(const FAetherInspectionSnapshot& S,const FAetherSkillDefinitionsV10& D,
    const FAetherSkillTreeLayout& Layout,const TOptional<FAetherSkillNodeIdentity>& Pending)
{
    FAetherSkillTreeModel M;M.Context=S.Context;
    if(!S.Context.IsValid()||!Layout.Validate(D,M.Message)||!S.Skills.Validate(D,M.Message)||
        !FAetherSkillStateV10::ValidateExternalGrants(S.ExternalGrants,D))
    {M.Message=TEXT("技能信息暂不可用。");return M;}
    M.bValid=true;M.AvailablePoints=S.Skills.AvailableSkillPoints;
    for(const auto& P:Layout.Nodes)
    {
        const auto& Def=D.Skills.FindChecked(P.Node.SkillId);
        const int32 Permanent=S.Skills.PermanentRank(Def.SkillId),Effective=S.Skills.EffectiveRank(Def.SkillId,S.ExternalGrants);
        const auto Allowed=S.Skills.CanLearnNext(Def.SkillId,S.SkillContext,D);
        FAetherSkillTreeNode N;N.Identity=P.Node;N.Position=P.Position;N.Title=Def.DisplayName;N.IconId=Def.IconId;N.Mechanic=Def.Mechanic;
        N.bPermanent=P.Node.Rank<=Permanent;N.bAuthorized=P.Node.Rank<=Effective;
        N.Reason=AetherInspection::SkillReason(Allowed);
        if(Pending.IsSet()&&Pending.GetValue()==P.Node){N.State=EAetherSkillNodeState::Pending;N.Reason=TEXT("等待提交确认。");}
        else if(N.bPermanent){N.State=Permanent==Def.Ranks.Num()?EAetherSkillNodeState::Maximum:EAetherSkillNodeState::Learned;N.Reason=TEXT("已获得永久授权。");}
        else if(P.Node.Rank>Permanent+1){N.State=EAetherSkillNodeState::Prerequisite;N.Reason=TEXT("请先学习同路线的上一级。");}
        else
        {
            using E=EAetherSkillMutationCode;
            switch(Allowed)
            {
            case E::Applied:N.State=EAetherSkillNodeState::Available;break;
            case E::StoryRequired:case E::QuestRequired:N.State=EAetherSkillNodeState::Unopened;break;
            case E::Prerequisite:N.State=EAetherSkillNodeState::Prerequisite;break;
            case E::InsufficientPoints:N.State=EAetherSkillNodeState::Insufficient;break;
            default:N.State=EAetherSkillNodeState::Blocked;break;
            }
        }
        if(N.State==EAetherSkillNodeState::Learned&&N.Identity.Rank==Permanent&&Allowed==EAetherSkillMutationCode::Applied)
        {N.State=EAetherSkillNodeState::Upgradeable;N.Reason=TEXT("满足下一级条件，可查看并确认升级。");}
        if(N.bAuthorized&&!N.bPermanent&&N.State!=EAetherSkillNodeState::Pending)
        {N.State=EAetherSkillNodeState::Granted;N.Reason=TEXT("由装备或临时来源授权，尚未永久学习。");}
        M.Nodes.Add(MoveTemp(N));
    }
    auto Index=[&](const FString& Id,int32 Rank){return M.Nodes.IndexOfByPredicate([&](const auto& N){return N.Identity==FAetherSkillNodeIdentity{Id,Rank};});};
    for(int32 I=0;I<M.Nodes.Num();++I)
    {
        const auto& N=M.Nodes[I];const auto& Def=D.Skills.FindChecked(N.Identity.SkillId);
        if(N.Identity.Rank>1)M.Edges.Add({Index(Def.SkillId,N.Identity.Rank-1),I});
        else for(const auto& P:Def.Prerequisites)M.Edges.Add({Index(P.SkillId,P.Rank),I});
    }
    for(int32 Slot=0;Slot<FAetherSkillStateV10::HotbarCapacity;++Slot)
    {
        FAetherSkillHotbarView H;H.Slot=Slot;H.SkillId=S.Skills.Hotbar.FindRef(Slot);
        if(const auto* Def=D.Skills.Find(H.SkillId))
        {
            H.Title=Def->DisplayName;H.IconId=Def->IconId;H.EffectiveRank=S.Skills.EffectiveRank(H.SkillId,S.ExternalGrants);
            H.bAvailable=Def->bActive&&H.EffectiveRank>0;
        }
        M.Hotbar.Add(MoveTemp(H));
    }
    TArray<FString> Sources;S.Skills.PointEvents.GenerateKeyArray(Sources);Sources.Sort();
    for(const auto& Id:Sources)M.PointSources.Add({Id,S.Skills.PointEvents.FindChecked(Id)});
    return M;
}
int32 AetherSkillTree::Navigate(const FAetherSkillTreeModel& M,int32 Selected,FVector2D Direction)
{
    if(M.Nodes.IsEmpty())return INDEX_NONE;if(!M.Nodes.IsValidIndex(Selected))return 0;
    if(Direction.ContainsNaN()||Direction.IsNearlyZero())return Selected;
    Direction.Normalize();TArray<int32> Linked;
    if(FMath::Abs(Direction.X)>.5)
        for(const auto& Edge:M.Edges)
        {
            if(Direction.X>0&&Edge.From==Selected)Linked.AddUnique(Edge.To);
            if(Direction.X<0&&Edge.To==Selected)Linked.AddUnique(Edge.From);
        }
    // 同一路线的等级优先，随后再考虑跨路线的分支/合流边。
    for(int32 Index:Linked)if(M.Nodes.IsValidIndex(Index)&&M.Nodes[Index].Identity.SkillId==M.Nodes[Selected].Identity.SkillId)return Index;
    if(!Linked.IsEmpty())return Linked[0];
    int32 Best=Selected;double Score=MAX_dbl;
    for(int32 I=0;I<M.Nodes.Num();++I)
    {
        if(I==Selected)continue;const FVector2D Delta=M.Nodes[I].Position-M.Nodes[Selected].Position;
        const double Along=FVector2D::DotProduct(Delta,Direction);if(Along<=0)continue;
        const double Across=FMath::Abs(Delta.X*Direction.Y-Delta.Y*Direction.X);
        const double Candidate=Delta.Size()+Across*3;
        if(Candidate<Score){Score=Candidate;Best=I;}
    }
    return Best;
}
