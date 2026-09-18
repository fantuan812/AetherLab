#include "Interaction/AetherInteractionDefinitions.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
namespace
{
bool Id(const FString& S)
{
    if(S.IsEmpty()||S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_'||C=='-'||C=='.'))return false;
    return true;
}
bool Text(const FString& S,int32 Limit)
{
    if(S.IsEmpty()||S.Len()>Limit)return false;
    for(TCHAR C:S)if(C<32&&C!=10)return false;
    FTCHARToUTF8 U(*S);FUTF8ToTCHAR Back(U.Get(),U.Length());return FString(Back.Length(),Back.Get())==S;
}
bool Names(const TSharedPtr<FJsonObject>& O,const TCHAR* Field,TArray<FString>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!O->TryGetArrayField(Field,A)||A->Num()>32)return false;
    for(const auto& V:*A){FString S;if(!V->TryGetString(S)||!Id(S)||Out.Contains(S))return false;Out.Add(S);}return true;
}
bool Kind(const FString& S,EAetherInteractionActionKind& Out)
{
    static const TCHAR* Names[]={TEXT("Talk"),TEXT("TrackObjective"),TEXT("Register"),TEXT("BindInn"),TEXT("Rest"),
        TEXT("LearnStorySkills"),TEXT("Train"),TEXT("ResetSkills"),TEXT("Trade"),TEXT("Repair"),TEXT("ClaimQuest"),TEXT("ClaimSkillPoints")};
    for(int32 I=0;I<UE_ARRAY_COUNT(Names);++I)if(S.Equals(Names[I],ESearchCase::CaseSensitive)){Out=EAetherInteractionActionKind(I);return true;}return false;
}
}
bool FAetherInteractionDefinitions::Validate(const FAetherRules& Rules,const FAetherEconomyDefinitionsV10& Economy,FString& Reason) const
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(SchemaVersion!=1||Targets.IsEmpty()||Targets.Num()>128||!Rules.bValid)return Fail(TEXT("Invalid interaction definitions"));
    const auto References=[&](const TArray<FString>& Values)
    {
        if(Values.Num()>32)return false;TSet<FString> Seen;
        for(const auto& S:Values)
        {
            if(!Id(S)||Seen.Contains(S))return false;Seen.Add(S);
            // 引用使用规范大小写；不能让 FName 的大小写折叠掩盖错拼的持久 ID。
            const auto* Q=Rules.Quest(FName(*S));if(!Q||!Q->Id.ToString().Equals(S,ESearchCase::CaseSensitive))return false;
        }
        return true;
    };
    for(const auto& Pair:Targets)
    {
        const auto& D=Pair.Value;
        if(!Id(D.Id)||Pair.Key!=D.Id||D.Actions.Num()>16||D.Dialogue.Num()>64)return Fail(TEXT("Invalid target definition bounds"));
        TSet<FString> Actions;
        for(const auto& A:D.Actions)
        {
            if(!Id(A.Id)||Actions.Contains(A.Id)||uint8(A.Kind)>uint8(EAetherInteractionActionKind::ClaimSkillPoints)||
                !Text(A.Verb,64)||!Id(A.IconId)||A.Priority<0||A.Priority>1000)return Fail(TEXT("Invalid action identity/presentation"));
            Actions.Add(A.Id);
            if(!References(A.RequiredClaims)||!References(A.HideAfterClaims))return Fail(TEXT("Unknown/duplicate quest condition"));
            for(const auto* List:{&A.RequiredEvidence,&A.HideAfterEvidence})
            {
                if(List->Num()>32)return Fail(TEXT("Too many objective conditions"));TSet<FString> Seen;
                for(const auto& V:*List)
                {
                    bool Found=false;for(const auto& O:Rules.Objectives)if(O.Key.ToString().Equals(V,ESearchCase::CaseSensitive)){Found=true;break;}
                    if(!Id(V)||!Found||Seen.Contains(V))return Fail(TEXT("Unknown/duplicate objective condition"));Seen.Add(V);
                }
            }
            for(const auto& Q:A.RequiredClaims)if(A.HideAfterClaims.Contains(Q))return Fail(TEXT("Contradictory quest condition"));
            for(const auto& O:A.RequiredEvidence)if(A.HideAfterEvidence.Contains(O))return Fail(TEXT("Contradictory objective condition"));
            if(!A.QuestId.IsEmpty()&&(!Rules.Quest(FName(*A.QuestId))||!Rules.Quest(FName(*A.QuestId))->Id.ToString().Equals(A.QuestId,ESearchCase::CaseSensitive)))return Fail(TEXT("Unknown quest reference"));
            if(!A.ObjectiveId.IsEmpty())
            {
                bool Found=false;for(const auto& O:Rules.Objectives)if(O.Key.ToString().Equals(A.ObjectiveId,ESearchCase::CaseSensitive)){Found=true;break;}
                if(!Found)return Fail(TEXT("Unknown objective reference"));
            }
            using K=EAetherInteractionActionKind;
            if((A.Kind==K::Register&&!A.ObjectiveId.Equals(TEXT("Register"),ESearchCase::CaseSensitive))||
                (A.Kind==K::BindInn&&!A.ObjectiveId.Equals(TEXT("Inn"),ESearchCase::CaseSensitive)))return Fail(TEXT("Action kind/objective mismatch"));
            if(A.Kind==K::Talk){if(!D.Dialogue.Contains(A.DialogueId))return Fail(TEXT("Talk requires known dialogue"));}
            else if(!A.DialogueId.IsEmpty())return Fail(TEXT("Non-talk action cannot silently execute dialogue"));
            if(A.Kind==K::Trade||A.Kind==K::Repair)
            {
                const auto* Shop=Economy.Shops.Find(A.ServiceId);
                if(!Shop||!Shop->Id.Equals(A.ServiceId,ESearchCase::CaseSensitive)||(A.Kind==K::Repair&&!Shop->bRepair))return Fail(TEXT("Unknown shop/repair service"));
            }
            else if(!A.ServiceId.IsEmpty())return Fail(TEXT("Unexpected shop service"));
            if(A.Kind==K::ClaimQuest&&A.QuestId.IsEmpty())return Fail(TEXT("Claim requires quest"));
        }
        TSet<FString> Reachable;
        for(const auto& A:D.Actions)if(!A.DialogueId.IsEmpty())Reachable.Add(A.DialogueId);
        for(const auto& Node:D.Dialogue)
        {
            const auto& N=Node.Value;
            if(!Id(N.Id)||N.Id!=Node.Key||!Text(N.Speaker,64)||!Text(N.Text,2048)||N.Options.Num()>8)return Fail(TEXT("Invalid dialogue node"));
            TSet<FString> Labels;
            for(const auto& O:N.Options)
            {
                if(!Text(O.Label,128)||Labels.Contains(O.Label)||(!O.ActionId.IsEmpty()&&!Actions.Contains(O.ActionId))||
                    (!O.NextNodeId.IsEmpty()&&!D.Dialogue.Contains(O.NextNodeId))||
                    (!O.ActionId.IsEmpty()&&!O.NextNodeId.IsEmpty()))return Fail(TEXT("Invalid/ambiguous dialogue option"));
                Labels.Add(O.Label);
            }
        }
        // 允许返回形成环，但不接受没有任何入口的死节点。选项为空或两种引用均为空表示结束。
        for(int32 Pass=0;Pass<D.Dialogue.Num();++Pass)
        {
            const auto Before=Reachable.Num();const auto Current=Reachable.Array();
            for(const auto& N:Current)for(const auto& O:D.Dialogue[N].Options)if(!O.NextNodeId.IsEmpty())Reachable.Add(O.NextNodeId);
            if(Reachable.Num()==Before)break;
        }
        if(Reachable.Num()!=D.Dialogue.Num())return Fail(TEXT("Unreachable dialogue node"));
    }
    Reason.Reset();return true;
}
FAetherInteractionDefinitions FAetherInteractionDefinitions::Parse(const FString& Json,const FAetherRules& Rules,const FAetherEconomyDefinitionsV10& Economy,FString& Reason)
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return FAetherInteractionDefinitions();};
    TSharedPtr<FJsonObject> Root;double Schema=0;
    if(Json.Len()>1024*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Root->TryGetNumberField(TEXT("SchemaVersion"),Schema)||Schema!=1)return Fail(TEXT("Invalid interaction JSON/schema"));
    const TArray<TSharedPtr<FJsonValue>>* Targets=nullptr;
    if(!Root->TryGetArrayField(TEXT("Targets"),Targets)||Targets->IsEmpty()||Targets->Num()>128)return Fail(TEXT("Invalid target array"));
    FAetherInteractionDefinitions Result;
    for(const auto& V:*Targets)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherInteractionDefinition D;
        const TArray<TSharedPtr<FJsonValue>> *Actions=nullptr,*Nodes=nullptr;
        if(!V->TryGetObject(O)||!O||!O->IsValid()||!(*O)->TryGetStringField(TEXT("Id"),D.Id)||Result.Targets.Contains(D.Id)||
            !(*O)->TryGetArrayField(TEXT("Actions"),Actions)||Actions->Num()>16||!(*O)->TryGetArrayField(TEXT("Dialogue"),Nodes)||Nodes->Num()>64)return Fail(TEXT("Invalid target entry"));
        for(const auto& AV:*Actions)
        {
            const TSharedPtr<FJsonObject>* AO=nullptr;FAetherInteractionActionDefinition A;FString Type;double Priority=0;
            if(!AV->TryGetObject(AO)||!AO||!AO->IsValid())return Fail(TEXT("Invalid action object"));
            const auto& J=*AO;
            if(!J->TryGetStringField(TEXT("Id"),A.Id)||!J->TryGetStringField(TEXT("Kind"),Type)||!Kind(Type,A.Kind)||
                !J->TryGetStringField(TEXT("Verb"),A.Verb)||!J->TryGetStringField(TEXT("IconId"),A.IconId)||
                !J->TryGetStringField(TEXT("DialogueId"),A.DialogueId)||!J->TryGetStringField(TEXT("QuestId"),A.QuestId)||
                !J->TryGetStringField(TEXT("ObjectiveId"),A.ObjectiveId)||!J->TryGetStringField(TEXT("ServiceId"),A.ServiceId)||
                !J->TryGetNumberField(TEXT("Priority"),Priority)||!FMath::IsFinite(Priority)||Priority<0||Priority>1000||Priority!=FMath::FloorToDouble(Priority)||
                !J->TryGetBoolField(TEXT("SafeOnly"),A.bSafeOnly)||!J->TryGetBoolField(TEXT("HideLocked"),A.bHideLocked)||
                !Names(J,TEXT("RequiredClaims"),A.RequiredClaims)||!Names(J,TEXT("RequiredEvidence"),A.RequiredEvidence)||
                !Names(J,TEXT("HideAfterClaims"),A.HideAfterClaims)||!Names(J,TEXT("HideAfterEvidence"),A.HideAfterEvidence))return Fail(TEXT("Invalid finite action definition"));
            A.Priority=int32(Priority);D.Actions.Add(MoveTemp(A));
        }
        for(const auto& NV:*Nodes)
        {
            const TSharedPtr<FJsonObject>* NO=nullptr;FAetherDialogueNode N;const TArray<TSharedPtr<FJsonValue>>* Options=nullptr;
            if(!NV->TryGetObject(NO)||!NO||!NO->IsValid())return Fail(TEXT("Invalid node object"));
            const auto& J=*NO;
            if(!J->TryGetStringField(TEXT("Id"),N.Id)||D.Dialogue.Contains(N.Id)||!J->TryGetStringField(TEXT("Speaker"),N.Speaker)||
                !J->TryGetStringField(TEXT("Text"),N.Text)||!J->TryGetArrayField(TEXT("Options"),Options)||Options->Num()>8)return Fail(TEXT("Invalid dialogue"));
            for(const auto& OV:*Options)
            {
                const TSharedPtr<FJsonObject>* OO=nullptr;FAetherDialogueOption Option;
                if(!OV->TryGetObject(OO)||!OO||!OO->IsValid()||!(*OO)->TryGetStringField(TEXT("Label"),Option.Label)||
                    !(*OO)->TryGetStringField(TEXT("ActionId"),Option.ActionId)||!(*OO)->TryGetStringField(TEXT("NextNodeId"),Option.NextNodeId))return Fail(TEXT("Invalid dialogue option"));
                N.Options.Add(MoveTemp(Option));
            }
            const auto Key=N.Id;D.Dialogue.Add(Key,MoveTemp(N));
        }
        const auto Key=D.Id;Result.Targets.Add(Key,MoveTemp(D));
    }
    if(!Result.Validate(Rules,Economy,Reason))return {};return Result;
}
