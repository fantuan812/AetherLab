#include "AetherRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
const FAetherRules& FAetherRules::Get()
{
    static FAetherRules Rules=[]()
    {
        FAetherRules R;FString Text;TSharedPtr<FJsonObject> Root;
        if(!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/Rules.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid())
        {R.Error=TEXT("Cannot read AetherCore/Definitions/Rules.json");return R;}
        const TSharedPtr<FJsonObject>* Items=nullptr;const TArray<TSharedPtr<FJsonValue>>* Quests=nullptr;
        if(!Root->TryGetObjectField(TEXT("Items"),Items)||!Root->TryGetArrayField(TEXT("Quests"),Quests)||Quests->Num()!=8){R.Error=TEXT("Missing items or eight main quests");return R;}
        for(const auto& P:(*Items)->Values)
        {
            const auto O=P.Value->AsObject();if(!O.IsValid()){R.Error=TEXT("Invalid item");return R;}
            double Stack=0,Buy=0,Sell=0;if(!O->TryGetNumberField(TEXT("MaxStack"),Stack)||!O->TryGetNumberField(TEXT("Buy"),Buy)||!O->TryGetNumberField(TEXT("Sell"),Sell)||Stack<1||Stack>999||Buy<0||Sell<0||(Buy>0&&Sell>=Buy))
            {R.Error=TEXT("Invalid stack or economy price");return R;}
            R.Items.Add(*P.Key,{int32(Stack),int32(Buy),int32(Sell)});
        }
        TSet<FName> Seen;
        for(const auto& V:*Quests)
        {
            auto O=V->AsObject();FAetherQuestRule Q;FString Id;
            const TArray<TSharedPtr<FJsonValue>> *Prev=nullptr,*Objectives=nullptr;const TSharedPtr<FJsonObject>* Rewards=nullptr;
            double Gold=0,Experience=0;
            if(!O||!O->TryGetStringField(TEXT("Id"),Id)||!O->TryGetStringField(TEXT("Title"),Q.Title)||!O->TryGetArrayField(TEXT("Prerequisites"),Prev)||!O->TryGetArrayField(TEXT("Objectives"),Objectives)||!O->TryGetNumberField(TEXT("Gold"),Gold)||!O->TryGetNumberField(TEXT("Experience"),Experience)||!O->TryGetObjectField(TEXT("Items"),Rewards))
            {R.Error=TEXT("Incomplete quest definition");return R;}
            Q.Id=*Id;if(Q.Id.IsNone()||Seen.Contains(Q.Id)||Gold<0||Gold>10000||Experience<0||Experience>10000){R.Error=TEXT("Invalid quest ID/reward");return R;}Seen.Add(Q.Id);
            Q.Gold=int32(Gold);Q.Experience=int32(Experience);
            for(auto P:*Prev){int32 Index=int32(P->AsNumber());if(Index<0||Index>=R.Quests.Num()){R.Error=TEXT("Quest prerequisite cycle/order");return R;}Q.Prerequisites.AddUnique(Index);}
            for(auto P:*Objectives)Q.Objectives.AddUnique(*P->AsString());
            if(Q.Objectives.IsEmpty()||Q.Objectives.Contains(NAME_None)){R.Error=TEXT("Empty objective");return R;}
            for(auto P:(*Rewards)->Values){int32 Count=int32(P.Value->AsNumber());if(!R.Items.Contains(*P.Key)||Count<1||Count>1000){R.Error=TEXT("Unknown reward");return R;}Q.Items.Add(*P.Key,Count);}
            R.Quests.Add(Q);
        }
        const TSharedPtr<FJsonObject>* Encounters=nullptr;
        if(!Root->TryGetObjectField(TEXT("Encounters"),Encounters)){R.Error=TEXT("Missing encounter definitions");return R;}
        for(const auto& Pair:(*Encounters)->Values)
        {
            auto O=Pair.Value->AsObject();const TArray<TSharedPtr<FJsonValue>> *Center=nullptr,*Types=nullptr;double Respawn=0;
            if(!O||!O->TryGetArrayField(TEXT("Center"),Center)||Center->Num()!=3||!O->TryGetArrayField(TEXT("Types"),Types)||Types->IsEmpty()||Types->Num()>8||!O->TryGetNumberField(TEXT("RespawnSeconds"),Respawn)||Respawn<0)
            {R.Error=TEXT("Invalid encounter definition");return R;}
            FAetherEncounterRule Rule;Rule.Center=FVector((*Center)[0]->AsNumber(),(*Center)[1]->AsNumber(),(*Center)[2]->AsNumber());Rule.RespawnSeconds=Respawn;
            for(const auto& T:*Types){int32 Type=int32(T->AsNumber());if(Type<1||Type>5){R.Error=TEXT("Invalid enemy archetype");return R;}Rule.Types.Add(uint8(Type));}
            R.Encounters.Add(*Pair.Key,Rule);
        }
        R.bValid=true;return R;
    }();
    return Rules;
}
