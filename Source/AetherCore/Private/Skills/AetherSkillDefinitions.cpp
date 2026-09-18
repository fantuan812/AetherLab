#include "Skills/AetherSkillDefinitions.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
namespace
{
bool Id(const FString& S)
{
    if(S.IsEmpty()||S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))return false;
    return true;
}
bool Number(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,double& N,double Min,double Max)
{return O->TryGetNumberField(Key,N)&&FMath::IsFinite(N)&&N>=Min&&N<=Max;}
bool Integer(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,int32& N,int32 Min,int32 Max)
{double V=0;if(!Number(O,Key,V,Min,Max)||FMath::FloorToDouble(V)!=V)return false;N=int32(V);return true;}
}
const FAetherSkillDefinitionV10* FAetherSkillDefinitionsV10::Legacy(int32 Bit) const
{
    if(Bit<0||Bit>3)return nullptr;
    for(const auto& P:Skills)if(P.Value.LegacyBit==Bit)return &P.Value;
    return nullptr;
}
const FAetherSkillRankEffect* FAetherSkillDefinitionsV10::Effect(const FString& Id,int32 Rank) const
{
    const auto* D=Skills.Find(Id);return D&&Rank>=1&&D->Ranks.IsValidIndex(Rank-1)?&D->Ranks[Rank-1]:nullptr;
}
bool FAetherSkillDefinitionsV10::Validate(FString& Reason) const
{
    const auto Fail=[&](const TCHAR* Text){Reason=Text;return false;};
    if(ContentSchemaVersion!=3||Skills.IsEmpty()||Skills.Num()>128)return Fail(TEXT("Unsupported skill schema/count"));
    TSet<int32> LegacyBits;
    for(const auto& Pair:Skills)
    {
        const auto& D=Pair.Value;
        if(!Id(D.SkillId)||D.SkillId!=Pair.Key||D.DisplayName.IsEmpty()||D.DisplayName.Len()>128||!Id(D.IconId)||
            (!D.RequiredQuest.IsEmpty()&&!Id(D.RequiredQuest))||D.Ranks.IsEmpty()||D.Ranks.Num()>3||
            uint8(D.Mechanic)>3||D.Prerequisites.Num()>16||D.LegacyBit < -1||D.LegacyBit>3)
            return Fail(TEXT("Invalid skill definition"));
        if(D.LegacyBit>=0){if(LegacyBits.Contains(D.LegacyBit))return Fail(TEXT("Duplicate legacy skill bit"));LegacyBits.Add(D.LegacyBit);}
        TSet<FString> Seen;
        for(const auto& P:D.Prerequisites)
        {
            const auto* Parent=Skills.Find(P.SkillId);
            if(!Parent||P.SkillId==D.SkillId||P.Rank<1||P.Rank>Parent->Ranks.Num()||Seen.Contains(P.SkillId))
                return Fail(TEXT("Unknown/duplicate skill prerequisite"));
            // 必得故事基础不能反向依赖需要自由点数的节点，避免洗点/耗尽点数锁死主线。
            if(D.bStoryBase&&(!Parent->bStoryBase||P.Rank!=1))return Fail(TEXT("Story base depends on a paid rank"));
            Seen.Add(P.SkillId);
        }
        for(const auto& R:D.Ranks)
        {
            if(R.PointCost<0||R.PointCost>100||R.RequiredLevel<1||R.RequiredLevel>100||
                !FMath::IsFinite(R.ManaCost)||R.ManaCost<0||R.ManaCost>100||
                !FMath::IsFinite(R.Cooldown)||R.Cooldown<=0||R.Cooldown>60||
                !FMath::IsFinite(R.RangeCm)||R.RangeCm<1||R.RangeCm>10000||
                !FMath::IsFinite(R.TargetRadiusCm)||R.TargetRadiusCm<0||R.TargetRadiusCm>500||
                !FMath::IsFinite(R.HeatJ)||FMath::Abs(R.HeatJ)>1000000||
                !FMath::IsFinite(R.WaterKg)||R.WaterKg<0||R.WaterKg>3||
                !FMath::IsFinite(R.ElectricalJ)||R.ElectricalJ<0||R.ElectricalJ>100000)
                return Fail(TEXT("Invalid rank effect"));
            if((D.Mechanic==EAetherSkillMechanic::Fire&&(R.HeatJ<=0||R.WaterKg!=0||R.ElectricalJ!=0))||
                (D.Mechanic==EAetherSkillMechanic::Water&&(R.WaterKg<=0||R.HeatJ!=0||R.ElectricalJ!=0))||
                (D.Mechanic==EAetherSkillMechanic::Frost&&(R.HeatJ>=0||R.WaterKg!=0||R.ElectricalJ!=0))||
                (D.Mechanic==EAetherSkillMechanic::Lightning&&(R.ElectricalJ<=0||R.HeatJ!=0||R.WaterKg!=0)))
                return Fail(TEXT("Rank effect does not match finite mechanic"));
        }
        if(D.bStoryBase&&D.Ranks[0].PointCost!=0)return Fail(TEXT("Mandatory story base cannot cost free-spend points"));
    }
    // 三色 DFS 检查有向前置图。布局坐标不参与这个图，拖动节点不会改变合法学习顺序。
    TMap<FString,uint8> Color;
    TFunction<bool(const FString&)> Visit=[&](const FString& Key)
    {
        const uint8 State=Color.FindRef(Key);if(State==1)return false;if(State==2)return true;
        Color.Add(Key,1);
        for(const auto& P:Skills.FindChecked(Key).Prerequisites)if(!Visit(P.SkillId))return false;
        Color.Add(Key,2);return true;
    };
    for(const auto& P:Skills)if(!Visit(P.Key))return Fail(TEXT("Skill prerequisite cycle"));
    Reason.Reset();return true;
}
FAetherSkillDefinitionsV10 FAetherSkillDefinitionsV10::Parse(const FString& Json,FString& Reason)
{
    const auto Fail=[&](const TCHAR* Text){Reason=Text;return FAetherSkillDefinitionsV10();};
    FAetherSkillDefinitionsV10 D;TSharedPtr<FJsonObject> Root;
    if(Json.Len()>1024*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Integer(Root,TEXT("ContentSchemaVersion"),D.ContentSchemaVersion,3,3))return Fail(TEXT("Invalid skill JSON/schema"));
    const TArray<TSharedPtr<FJsonValue>>* Skills=nullptr;
    if(!Root->TryGetArrayField(TEXT("Skills"),Skills)||Skills->IsEmpty()||Skills->Num()>128)return Fail(TEXT("Invalid skill array"));
    for(const auto& Value:*Skills)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherSkillDefinitionV10 S;int32 Mechanic=0;
        if(!Value->TryGetObject(O)||!O||!O->IsValid()||!(*O)->TryGetStringField(TEXT("SkillId"),S.SkillId)||
            !(*O)->TryGetStringField(TEXT("DisplayName"),S.DisplayName)||!(*O)->TryGetStringField(TEXT("IconId"),S.IconId)||
            !(*O)->TryGetStringField(TEXT("RequiredQuest"),S.RequiredQuest)||!(*O)->TryGetBoolField(TEXT("Active"),S.bActive)||
            !(*O)->TryGetBoolField(TEXT("StoryBase"),S.bStoryBase)||!Integer(*O,TEXT("Mechanic"),Mechanic,0,3)||
            !Integer(*O,TEXT("LegacyBit"),S.LegacyBit,-1,3))return Fail(TEXT("Incomplete skill definition"));
        S.Mechanic=EAetherSkillMechanic(Mechanic);
        const TArray<TSharedPtr<FJsonValue>> *Ranks=nullptr,*Parents=nullptr;
        if(!(*O)->TryGetArrayField(TEXT("Ranks"),Ranks)||Ranks->IsEmpty()||Ranks->Num()>3||
            !(*O)->TryGetArrayField(TEXT("Prerequisites"),Parents)||Parents->Num()>16)return Fail(TEXT("Invalid ranks/prerequisites"));
        for(const auto& V:*Ranks)
        {
            const TSharedPtr<FJsonObject>* R=nullptr;FAetherSkillRankEffect E;
            if(!V->TryGetObject(R)||!R||!R->IsValid()||
                !Integer(*R,TEXT("PointCost"),E.PointCost,0,100)||!Integer(*R,TEXT("RequiredLevel"),E.RequiredLevel,1,100)||
                !Number(*R,TEXT("ManaCost"),E.ManaCost,0,100)||!Number(*R,TEXT("Cooldown"),E.Cooldown,0.01,60)||
                !Number(*R,TEXT("RangeCm"),E.RangeCm,1,10000)||!Number(*R,TEXT("TargetRadiusCm"),E.TargetRadiusCm,0,500)||
                !Number(*R,TEXT("HeatJ"),E.HeatJ,-1000000,1000000)||!Number(*R,TEXT("WaterKg"),E.WaterKg,0,3)||
                !Number(*R,TEXT("ElectricalJ"),E.ElectricalJ,0,100000))return Fail(TEXT("Incomplete rank effect"));
            S.Ranks.Add(E);
        }
        for(const auto& V:*Parents)
        {
            const TSharedPtr<FJsonObject>* P=nullptr;FAetherSkillPrerequisite Parent;
            if(!V->TryGetObject(P)||!P||!P->IsValid()||!(*P)->TryGetStringField(TEXT("SkillId"),Parent.SkillId)||
                !Integer(*P,TEXT("Rank"),Parent.Rank,1,3))return Fail(TEXT("Invalid prerequisite"));
            S.Prerequisites.Add(MoveTemp(Parent));
        }
        if(D.Skills.Contains(S.SkillId))return Fail(TEXT("Duplicate skill ID"));
        const FString Key=S.SkillId;D.Skills.Add(Key,MoveTemp(S));
    }
    if(!D.Validate(Reason))return FAetherSkillDefinitionsV10();
    return D;
}
const FAetherSkillDefinitionsV10& FAetherSkillDefinitionsV10::Get()
{
    static const FAetherSkillDefinitionsV10 Definitions=[]
    {
        FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Skills.json")));
        return Parse(Json,Reason);
    }();
    return Definitions;
}
