#include "Effects/AetherBuffState.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FAetherBuffDefinitions FAetherBuffDefinitions::Parse(const FString& Json)
{
    FAetherBuffDefinitions Out;
    const auto Fail=[&](const TCHAR* Why){FAetherBuffDefinitions Invalid;Invalid.Error=Why;return Invalid;};
    TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    double Schema=0;
    if(Json.Len()>1024*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Root->TryGetNumberField(TEXT("Schema"),Schema)||Schema!=1||!Root->TryGetArrayField(TEXT("Buffs"),Rows)||Rows->IsEmpty()||Rows->Num()>128)
        return Fail(TEXT("Invalid buff catalog/schema"));
    const auto Integer=[](const TSharedPtr<FJsonObject>& O,const TCHAR* Key,int32& Value,int32 Min,int32 Max) {
        double N=0;if(!O->TryGetNumberField(Key,N)||!FMath::IsFinite(N)||N<Min||N>Max||N!=FMath::FloorToDouble(N))return false;
        Value=int32(N);return true;
    };
    const auto Strings=[](const TSharedPtr<FJsonObject>& O,const TCHAR* Key,TArray<FString>& Values) {
        const TArray<TSharedPtr<FJsonValue>>* Array=nullptr;if(!O->TryGetArrayField(Key,Array)||Array->Num()>16)return false;
        for(const auto& V:*Array){FString S;if(!V->TryGetString(S)||S.IsEmpty()||S.Len()>96||Values.Contains(S))return false;Values.Add(S);}return true;
    };
    for(const auto& Row:*Rows)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FAetherBuffDefinition D;int32 Reapply=0,Overflow=0;
        if(!Row->TryGetObject(O)||!O||!O->IsValid()||
            !(*O)->TryGetStringField(TEXT("Id"),D.Id)||!(*O)->TryGetStringField(TEXT("DisplayName"),D.DisplayName)||
            !(*O)->TryGetStringField(TEXT("IconId"),D.IconId)||!(*O)->TryGetStringField(TEXT("Description"),D.Description)||
            !(*O)->TryGetStringField(TEXT("ExclusiveGroup"),D.ExclusiveGroup)||
            !Integer(*O,TEXT("Revision"),D.Revision,1,100000)||!Integer(*O,TEXT("Priority"),D.Priority,0,1000)||
            !Integer(*O,TEXT("MaxStacks"),D.MaxStacks,1,32)||!Integer(*O,TEXT("Reapply"),Reapply,0,4)||
            !Integer(*O,TEXT("Overflow"),Overflow,0,2)||!(*O)->TryGetNumberField(TEXT("Duration"),D.Duration)||
            !(*O)->TryGetNumberField(TEXT("Period"),D.Period)||!(*O)->TryGetBoolField(TEXT("PerSource"),D.bPerSource)||
            !(*O)->TryGetBoolField(TEXT("ResetPeriod"),D.bResetPeriod)||!(*O)->TryGetBoolField(TEXT("Dispellable"),D.bDispellable)||
            !Strings(*O,TEXT("Tags"),D.Tags)||!Strings(*O,TEXT("DispelTags"),D.DispelTags)||!Strings(*O,TEXT("ImmunityTags"),D.ImmunityTags))
            return Fail(TEXT("Incomplete buff definition"));
        D.Reapply=EAetherBuffReapply(Reapply);D.Overflow=EAetherBuffOverflow(Overflow);
        const TArray<TSharedPtr<FJsonValue>>* Operations=nullptr;
        if(!(*O)->TryGetArrayField(TEXT("Operations"),Operations)||Operations->Num()>16)return Fail(TEXT("Invalid buff operations"));
        for(const auto& Value:*Operations)
        {
            const TSharedPtr<FJsonObject>* Op=nullptr;FAetherBuffOperation B;int32 Kind=0,Operation=0;
            if(!Value->TryGetObject(Op)||!Op||!Op->IsValid()||!Integer(*Op,TEXT("Kind"),Kind,0,4)||
                !Integer(*Op,TEXT("Operation"),Operation,0,3)||!(*Op)->TryGetStringField(TEXT("Id"),B.Id)||
                !(*Op)->TryGetNumberField(TEXT("Value"),B.Value))return Fail(TEXT("Invalid buff operation"));
            B.Kind=EAetherBuffOperation(Kind);B.AttributeOperation=EAetherAttributeOperation(Operation);D.Operations.Add(MoveTemp(B));
        }
        if(Out.Buffs.Contains(D.Id)||!D.Validate(Out.Error))return Fail(TEXT("Duplicate or invalid buff"));
        const FString Id=D.Id;Out.Buffs.Add(Id,MoveTemp(D));
    }
    Out.bValid=true;return Out;
}
const FAetherBuffDefinitions& FAetherBuffDefinitions::Get()
{
    static const FAetherBuffDefinitions D=[] {
        FString Json;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Buffs.json")));
        return Parse(Json);
    }();return D;
}
