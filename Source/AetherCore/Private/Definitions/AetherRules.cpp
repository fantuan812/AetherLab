#include "Definitions/AetherRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
bool AetherReadExtensions(FAetherRules& R,const TSharedPtr<FJsonObject>& Root);
FAetherRules FAetherRules::Parse(const FString& Text)
{
        FAetherRules R;TSharedPtr<FJsonObject> Root;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid())
        {R.Error=TEXT("Cannot read AetherCore/Definitions/Rules.json");return R;}
        const TSharedPtr<FJsonObject>* Items=nullptr;const TArray<TSharedPtr<FJsonValue>>* Quests=nullptr;
        if(!Root->TryGetObjectField(TEXT("Items"),Items)||!Root->TryGetArrayField(TEXT("Quests"),Quests)||Quests->IsEmpty()||Quests->Num()>128){R.Error=TEXT("Missing items or invalid quest count");return R;}
        for(const auto& P:(*Items)->Values)
        {
            const auto O=P.Value->AsObject();if(!O.IsValid()){R.Error=TEXT("Invalid item");return R;}
            double Stack=0,Buy=0,Sell=0;if(!O->TryGetNumberField(TEXT("MaxStack"),Stack)||!O->TryGetNumberField(TEXT("Buy"),Buy)||!O->TryGetNumberField(TEXT("Sell"),Sell)||!FMath::IsFinite(Stack)||!FMath::IsFinite(Buy)||!FMath::IsFinite(Sell)||P.Key.IsEmpty()||Stack<1||Stack>999||Buy<0||Sell<0||(Buy>0&&Sell>=Buy))
            {R.Error=TEXT("Invalid stack or economy price");return R;}
            if(FMath::FloorToDouble(Stack)!=Stack||FMath::FloorToDouble(Buy)!=Buy||FMath::FloorToDouble(Sell)!=Sell||Buy>10000000||Sell>10000000)
            {R.Error=TEXT("Non-integral item economy");return R;}
            FAetherItemRule Rule;Rule.MaxStack=int32(Stack);Rule.Buy=int32(Buy);Rule.Sell=int32(Sell);
            FString Equipment,Slot;const TArray<TSharedPtr<FJsonValue>>* Occupied=nullptr;
            if(!O->TryGetBoolField(TEXT("PlayerEquippable"),Rule.bPlayerEquippable)||!O->TryGetBoolField(TEXT("Removable"),Rule.bRemovable)||!O->TryGetBoolField(TEXT("Sellable"),Rule.bSellable)
                ||!O->TryGetStringField(TEXT("EquipmentId"),Equipment)||!O->TryGetStringField(TEXT("Slot"),Slot)||!O->TryGetArrayField(TEXT("OccupiedSlots"),Occupied))
            {R.Error=TEXT("Missing item capabilities");return R;}
            Rule.EquipmentId=*Equipment;Rule.Slot=*Slot;
            for(const auto& V:*Occupied){FString S;if(!V->TryGetString(S)||S.IsEmpty()||Rule.OccupiedSlots.Contains(*S)){R.Error=TEXT("Invalid occupied slots");return R;}Rule.OccupiedSlots.Add(*S);}
            if((Rule.bSellable&&(!Rule.bRemovable||Rule.Sell<=0))||(Rule.bPlayerEquippable&&(Rule.EquipmentId.IsNone()||Rule.Slot.IsNone()||Rule.MaxStack!=1||!Rule.OccupiedSlots.Contains(Rule.Slot)))
                ||Rule.OccupiedSlots.Num()>8){R.Error=TEXT("Inconsistent item capabilities");return R;}
            R.Items.Add(*P.Key,Rule);
        }
        TSet<FName> Seen;
        for(const auto& V:*Quests)
        {
            auto O=V->AsObject();FAetherQuestRule Q;FString Id;
            const TArray<TSharedPtr<FJsonValue>> *Prev=nullptr,*Objectives=nullptr;const TSharedPtr<FJsonObject>* Rewards=nullptr;
            double Gold=0,Experience=0;
            if(!O||!O->TryGetStringField(TEXT("Id"),Id)||!O->TryGetStringField(TEXT("Title"),Q.Title)||!O->TryGetArrayField(TEXT("Prerequisites"),Prev)||!O->TryGetArrayField(TEXT("Objectives"),Objectives)||!O->TryGetNumberField(TEXT("Gold"),Gold)||!O->TryGetNumberField(TEXT("Experience"),Experience)||!O->TryGetObjectField(TEXT("Items"),Rewards))
            {R.Error=TEXT("Incomplete quest definition");return R;}
            Q.Id=*Id;if(Q.Id.IsNone()||Seen.Contains(Q.Id)||!FMath::IsFinite(Gold)||!FMath::IsFinite(Experience)||FMath::FloorToDouble(Gold)!=Gold||FMath::FloorToDouble(Experience)!=Experience||Gold<0||Gold>10000||Experience<0||Experience>10000){R.Error=TEXT("Invalid quest ID/reward");return R;}Seen.Add(Q.Id);
            Q.Gold=int32(Gold);Q.Experience=int32(Experience);
            for(auto P:*Prev){FString Dependency;if(!P->TryGetString(Dependency)||Dependency.IsEmpty()||Q.Prerequisites.Contains(*Dependency)){R.Error=TEXT("Invalid prerequisite ID");return R;}Q.Prerequisites.Add(*Dependency);}
            if(!O->TryGetBoolField(TEXT("AutoClaim"),Q.bAutoClaim)||!O->TryGetBoolField(TEXT("BindInn"),Q.bBindInn))
            {R.Error=TEXT("Missing quest reward policy");return R;}
            for(auto P:*Objectives){FString Fact;if(!P->TryGetString(Fact)||Fact.IsEmpty()||Q.Objectives.Contains(*Fact)){R.Error=TEXT("Invalid objective ID");return R;}Q.Objectives.Add(*Fact);}
            if(Q.Objectives.IsEmpty()||Q.Objectives.Contains(NAME_None)){R.Error=TEXT("Empty objective");return R;}
            for(auto P:(*Rewards)->Values){double Value=0;if(!P.Value->TryGetNumber(Value)||!FMath::IsFinite(Value)||FMath::FloorToDouble(Value)!=Value||!R.Items.Contains(*P.Key)||Value<1||Value>1000){R.Error=TEXT("Unknown reward");return R;}Q.Items.Add(*P.Key,int32(Value));}
            R.Quests.Add(Q);
        }
        const TSharedPtr<FJsonObject>* Objectives=nullptr;
        if(!Root->TryGetObjectField(TEXT("Objectives"),Objectives)){R.Error=TEXT("Missing objective guidance");return R;}
        for(const auto& Pair:(*Objectives)->Values)
        {
            auto O=Pair.Value->AsObject();FAetherObjectiveRule Rule;FString Anchor;const TArray<TSharedPtr<FJsonValue>>* Position=nullptr;
            if(!O||!O->TryGetStringField(TEXT("Label"),Rule.Label)||Rule.Label.IsEmpty()||!O->TryGetStringField(TEXT("Hint"),Rule.Hint)||!O->TryGetStringField(TEXT("Anchor"),Anchor)||Anchor.IsEmpty()||!O->TryGetArrayField(TEXT("Position"),Position)||Position->Num()!=3)
            {R.Error=TEXT("Invalid objective guidance");return R;}
            Rule.Anchor=*Anchor;for(int I=0;I<3;++I){double Value=0;if(!(*Position)[I]->TryGetNumber(Value)||!FMath::IsFinite(Value)||FMath::Abs(Value)>1000000){R.Error=TEXT("Invalid objective coordinates");return R;}Rule.Position[I]=Value;}
            FString Scope;const TArray<TSharedPtr<FJsonValue>>* Sources=nullptr;
            if(!O->TryGetStringField(TEXT("Scope"),Scope)||!O->TryGetBoolField(TEXT("Retroactive"),Rule.bRetroactive)||!O->TryGetBoolField(TEXT("InspectableFire"),Rule.bInspectableFire)||!O->TryGetArrayField(TEXT("FactSources"),Sources))
            {R.Error=TEXT("Missing objective scope policy");return R;}
            if(Scope=="World")Rule.Scope=EAetherObjectiveScope::World;
            else if(Scope=="Party")Rule.Scope=EAetherObjectiveScope::Party;
            else if(Scope=="Daily")Rule.Scope=EAetherObjectiveScope::Daily;
            else if(Scope!="Personal"){R.Error=TEXT("Unknown objective scope");return R;}
            for(auto Source:*Sources){FString Id;if(!Source->TryGetString(Id)||Id.IsEmpty()){R.Error=TEXT("Invalid fact source");return R;}Rule.FactSources.AddUnique(*Id);}
            if((Rule.bRetroactive&&Rule.Scope!=EAetherObjectiveScope::World)||(Rule.Scope==EAetherObjectiveScope::World&&Rule.FactSources.IsEmpty()))
            {R.Error=TEXT("Unsafe world fact policy");return R;}
            R.Objectives.Add(*Pair.Key,Rule);
        }
        for(const auto& Q:R.Quests)for(FName Id:Q.Objectives)if(!R.Objectives.Contains(Id)){R.Error=TEXT("Missing quest objective label/anchor");return R;}
        // Validate the graph after all definitions exist. Array order is presentation only.
        TSet<FName> Visiting,Visited;
        TFunction<bool(FName)> Visit=[&](FName Id)
        {
            if(Visiting.Contains(Id))return false;if(Visited.Contains(Id))return true;
            const auto* Q=R.Quest(Id);if(!Q)return false;Visiting.Add(Id);
            for(FName Prev:Q->Prerequisites)if(!Visit(Prev))return false;
            Visiting.Remove(Id);Visited.Add(Id);return true;
        };
        for(const auto& Q:R.Quests)if(!Visit(Q.Id)){R.Error=TEXT("Unknown prerequisite or quest cycle");return R;}
        const TSharedPtr<FJsonObject>* Container=nullptr;
        if(!Root->TryGetObjectField(TEXT("Container"),Container)||!(*Container)->TryGetNumberField(TEXT("PourKg"),R.PourKg)||!(*Container)->TryGetNumberField(TEXT("RangeCm"),R.PourRangeCm)
            ||!FMath::IsFinite(R.PourKg)||R.PourKg<=0||R.PourKg>10||!FMath::IsFinite(R.PourRangeCm)||R.PourRangeCm<=0||R.PourRangeCm>1000)
        {R.Error=TEXT("Invalid container policy");return R;}
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
        if(!AetherReadExtensions(R,Root))return R;
        R.bValid=true;return R;
}
const FAetherQuestRule* FAetherRules::Quest(FName Id) const
{return Quests.FindByPredicate([Id](const auto& Q){return Q.Id==Id;});}
const FAetherRules& FAetherRules::Get()
{
    static const FAetherRules Rules=[](){FString Text;
        FString File=FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/Rules.json");
#if !UE_BUILD_SHIPPING
        FString Fixture;
        if(FParse::Value(FCommandLine::Get(),TEXT("AetherRulesFixture="),Fixture))
        {
            if(Fixture.IsEmpty()||Fixture.Len()>64||FPaths::GetCleanFilename(Fixture)!=Fixture||Fixture.Contains(TEXT(":"))||Fixture.Contains(TEXT("..")))
            {FAetherRules R;R.Error=TEXT("Invalid isolated rules fixture name");return R;}
            File=FPaths::ProjectSavedDir()/TEXT("Automation")/Fixture;
        }
#endif
        if(!FFileHelper::LoadFileToString(Text,*File)){FAetherRules R;R.Error=TEXT("Cannot read Rules.json");return R;}
        return Parse(Text);
    }();return Rules;
}
