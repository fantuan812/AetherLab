#include "Combat/AetherControlledActionDefinition.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <initializer_list>

bool FAetherControlledActionDefinition::IsValid() const
{
    const float Numbers[]={Duration,CommitTime,Cost,Cooldown,MotionSpeed,MotionTime,InvulnerabilityTime,Impulse};
    for(float V:Numbers)if(!FMath::IsFinite(V))return false;
    float Total=0;for(float Phase:PhaseDurations){if(!FMath::IsFinite(Phase)||Phase<=0)return false;Total+=Phase;}
    return !ActionId.IsNone()&&DefinitionVersion==1&&Duration>0&&Duration<=10&&
        (CommitTime==-1||(CommitTime>=0&&CommitTime<=Duration))&&
        Cost>=0&&Cooldown>=0&&MotionSpeed>=0&&MotionTime>=0&&MotionTime<=Duration&&
        InvulnerabilityTime>=0&&InvulnerabilityTime<=Duration&&Impulse>=0&&AllowedStances>0&&AllowedStances<=15&&
        (PhaseDurations.IsEmpty()||FMath::IsNearlyEqual(Total,Duration,KINDA_SMALL_NUMBER));
}
namespace
{
bool Fields(const FJsonObject& O,std::initializer_list<const TCHAR*> Expected)
{
    if(O.Values.Num()!=int32(Expected.size()))return false;
    for(const auto& Field:O.Values)
    {
        bool Matched=false;for(const TCHAR* Key:Expected)Matched|=Field.Key.Equals(Key,ESearchCase::CaseSensitive);
        if(!Matched)return false;
    }
    return true;
}
bool Number(const FJsonObject& O,const TCHAR* Key,float& Out)
{
    double Value=0;if(!O.TryGetNumberField(Key,Value)||!FMath::IsFinite(Value)||FMath::Abs(Value)>MAX_flt)return false;
    Out=float(Value);return true;
}
}
FAetherControlledActionCatalog FAetherControlledActionCatalog::Parse(const FString& Json)
{
    FAetherControlledActionCatalog D;
    const auto Fail=[&](const TCHAR* Why){FAetherControlledActionCatalog Bad;Bad.Error=Why;return Bad;};
    TSharedPtr<FJsonObject> Root;double Schema=0;
    if(Json.Len()>128*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||
        !Fields(*Root,{TEXT("SchemaVersion"),TEXT("Input"),TEXT("Actions")})||
        !Root->TryGetNumberField(TEXT("SchemaVersion"),Schema)||Schema!=1)return Fail(TEXT("Invalid action catalog schema"));
    const TSharedPtr<FJsonObject>* Input=nullptr;
    if(!Root->TryGetObjectField(TEXT("Input"),Input)||!Input||!Input->IsValid()||
        !Fields(**Input,{TEXT("AttackCharge"),TEXT("AttackBuffer")})||
        !Number(**Input,TEXT("AttackCharge"),D.AttackCharge)||D.AttackCharge<=0||D.AttackCharge>10||
        !Number(**Input,TEXT("AttackBuffer"),D.AttackBuffer)||D.AttackBuffer<0||D.AttackBuffer>D.AttackCharge)
        return Fail(TEXT("Invalid attack input timing"));
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Root->TryGetArrayField(TEXT("Actions"),Rows)||Rows->IsEmpty()||Rows->Num()>128)return Fail(TEXT("Invalid action rows"));
    TSet<FName> Seen;TMap<FName,FString> AuthoredNames;
    for(const auto& Value:*Rows)
    {
        const TSharedPtr<FJsonObject>* Object=nullptr;
        if(!Value->TryGetObject(Object)||!Object||!Object->IsValid())return Fail(TEXT("Action must be an object"));
        const auto& O=**Object;FAetherControlledActionDefinition R;FString Id,Contact,Cancel;double Stances=0;
        if(!Fields(O,{TEXT("ActionId"),TEXT("Duration"),TEXT("CommitTime"),TEXT("Cost"),TEXT("Cooldown"),
            TEXT("MotionSpeed"),TEXT("MotionTime"),TEXT("InvulnerabilityTime"),TEXT("Impulse"),TEXT("Loop"),TEXT("ContactPolicy"),
            TEXT("CancelPolicy"),TEXT("AllowedStances"),TEXT("PhaseDurations")})||
            !O.TryGetStringField(TEXT("ActionId"),Id)||Id.IsEmpty()||Id.Len()>64||
            !O.TryGetStringField(TEXT("ContactPolicy"),Contact)||!O.TryGetStringField(TEXT("CancelPolicy"),Cancel)||
            !O.TryGetBoolField(TEXT("Loop"),R.bLoop)||!O.TryGetNumberField(TEXT("AllowedStances"),Stances)||
            !FMath::IsFinite(Stances)||Stances<1||Stances>15||FMath::FloorToDouble(Stances)!=Stances)
            return Fail(TEXT("Invalid action fields"));
        for(TCHAR C:Id)if(!FChar::IsAlnum(C)&&C!='_')return Fail(TEXT("Invalid action identity"));
        R.ActionId=FName(*Id);R.AllowedStances=int32(Stances);
        if(Seen.Contains(R.ActionId))return Fail(TEXT("Duplicate action identity"));Seen.Add(R.ActionId);AuthoredNames.Add(R.ActionId,Id);
        if(Contact==TEXT("None"))R.ContactPolicy=EAetherActionContactPolicy::None;
        else if(Contact==TEXT("FixedObject"))R.ContactPolicy=EAetherActionContactPolicy::FixedObject;
        else if(Contact==TEXT("Weapon"))R.ContactPolicy=EAetherActionContactPolicy::Weapon;
        else if(Contact==TEXT("Ground"))R.ContactPolicy=EAetherActionContactPolicy::Ground;
        else return Fail(TEXT("Unknown action contact policy"));
        if(Cancel==TEXT("PresentationOnly"))R.CancelPolicy=EAetherActionCancelPolicy::PresentationOnly;
        else if(Cancel==TEXT("CancelBeforeCommitKeepCommitted"))R.CancelPolicy=EAetherActionCancelPolicy::CancelBeforeCommitKeepCommitted;
        else if(Cancel==TEXT("AbilityOwned"))R.CancelPolicy=EAetherActionCancelPolicy::AbilityOwned;
        else return Fail(TEXT("Unknown action cancel policy"));
        if(!Number(O,TEXT("Duration"),R.Duration)||!Number(O,TEXT("CommitTime"),R.CommitTime)||
            !Number(O,TEXT("Cost"),R.Cost)||!Number(O,TEXT("Cooldown"),R.Cooldown)||
            !Number(O,TEXT("MotionSpeed"),R.MotionSpeed)||!Number(O,TEXT("MotionTime"),R.MotionTime)||
            !Number(O,TEXT("InvulnerabilityTime"),R.InvulnerabilityTime)||!Number(O,TEXT("Impulse"),R.Impulse))
            return Fail(TEXT("Invalid action numeric field"));
        const TArray<TSharedPtr<FJsonValue>>* Phases=nullptr;
        if(!O.TryGetArrayField(TEXT("PhaseDurations"),Phases)||Phases->Num()>16)return Fail(TEXT("Invalid action phases"));
        for(const auto& Phase:*Phases){double V=0;if(!Phase->TryGetNumber(V)||!FMath::IsFinite(V)||V<=0||V>10)return Fail(TEXT("Invalid action phase duration"));R.PhaseDurations.Add(float(V));}
        if(!R.IsValid())return Fail(TEXT("Inconsistent action timing or stance"));
        D.Actions.Add(MoveTemp(R));
    }
    // Registered executors/authoring recipes support these exact identities and stance capabilities.
    // This registry carries no timing/balance values; unknown or differently-cased names are not aliases.
    const TMap<FString,int32> Capabilities={
        {TEXT("CrouchIdle"),2},{TEXT("CrouchWalk"),2},{TEXT("CrouchBack"),2},{TEXT("CrouchLeft"),2},{TEXT("CrouchRight"),2},
        {TEXT("CarryIdle"),4},{TEXT("CarryWalk"),4},{TEXT("Guard"),1},
        {TEXT("DodgeForward"),1},{TEXT("DodgeBack"),1},{TEXT("DodgeLeft"),1},{TEXT("DodgeRight"),1},
        {TEXT("Push"),1},{TEXT("Pickup"),1},{TEXT("PutDown"),4},{TEXT("Throw"),4},
        {TEXT("Rescue"),1},{TEXT("Cast"),1},{TEXT("Vault"),1},{TEXT("Stun"),7},{TEXT("Death"),15},
        {TEXT("GetUp"),8},{TEXT("Hit"),7},{TEXT("Land"),1},{TEXT("LandHeavy"),1}};
    if(D.Actions.Num()!=Capabilities.Num())return Fail(TEXT("Action capabilities do not match registered executors"));
    for(const auto& R:D.Actions)
    {
        const int32* Stances=nullptr;
        for(const auto& Capability:Capabilities)
            if(AuthoredNames.FindChecked(R.ActionId).Equals(Capability.Key,ESearchCase::CaseSensitive)){Stances=&Capability.Value;break;}
        if(!Stances||*Stances!=R.AllowedStances)return Fail(TEXT("Unknown action identity/case or unsupported stance capability"));
        const bool IsDodge=R.ActionId.ToString().StartsWith(TEXT("Dodge"));
        const bool IsVault=R.ActionId==TEXT("Vault");
        const bool IsWorld=R.ActionId==TEXT("Push")||R.ActionId==TEXT("Pickup")||R.ActionId==TEXT("PutDown")||R.ActionId==TEXT("Throw");
        if(!IsVault&&!R.PhaseDurations.IsEmpty())return Fail(TEXT("Only vault consumes motion phases"));
        if(!IsDodge&&(R.Cost!=0||R.Cooldown!=0||R.InvulnerabilityTime!=0||R.MotionSpeed!=0))
            return Fail(TEXT("Action executor does not consume cost/cooldown/invulnerability/speed"));
        if(!IsDodge&&!IsVault&&R.MotionTime!=0)return Fail(TEXT("Action executor does not consume root motion time"));
        if(R.ActionId!=TEXT("Push")&&R.Impulse!=0)return Fail(TEXT("Only push consumes the configured impulse"));
        if(!IsDodge&&!IsVault&&!IsWorld&&(R.CommitTime!=-1||R.CancelPolicy!=EAetherActionCancelPolicy::PresentationOnly||R.ContactPolicy!=EAetherActionContactPolicy::None))
            return Fail(TEXT("Presentation action cannot own a gameplay commit/contact/cancellation"));
    }
    const auto Find=[&](const TCHAR* Id)->const FAetherControlledActionDefinition&
        {return *D.Actions.FindByPredicate([&](const auto& R){return R.ActionId==FName(Id);});};
    const auto& Dodge=Find(TEXT("DodgeForward"));
    for(const TCHAR* Id:{TEXT("DodgeForward"),TEXT("DodgeBack"),TEXT("DodgeLeft"),TEXT("DodgeRight")})
    {
        const auto& R=Find(Id);
        if(R.CancelPolicy!=EAetherActionCancelPolicy::AbilityOwned||R.ContactPolicy!=EAetherActionContactPolicy::Ground||
            R.bLoop||R.CommitTime!=0||R.AllowedStances!=Dodge.AllowedStances||R.Cost!=Dodge.Cost||R.Cooldown!=Dodge.Cooldown||R.InvulnerabilityTime!=Dodge.InvulnerabilityTime)
            return Fail(TEXT("Dodge directions must share the GAS cost/cooldown/invulnerability contract"));
    }
    const auto& Vault=Find(TEXT("Vault"));
    if(Vault.bLoop||Vault.AllowedStances!=1||Vault.MotionTime!=Vault.Duration||Vault.PhaseDurations.Num()!=3||Vault.ContactPolicy!=EAetherActionContactPolicy::Ground||
        Vault.CancelPolicy!=EAetherActionCancelPolicy::AbilityOwned||Vault.CommitTime!=0)
        return Fail(TEXT("Vault requires three ordered GAS motion phases"));
    for(const TCHAR* Id:{TEXT("Push"),TEXT("Pickup"),TEXT("PutDown"),TEXT("Throw")})
    {
        const auto& R=Find(Id);
        if(R.CommitTime<0||R.bLoop||R.ContactPolicy!=EAetherActionContactPolicy::FixedObject||
            R.CancelPolicy!=EAetherActionCancelPolicy::CancelBeforeCommitKeepCommitted)
            return Fail(TEXT("World actions require a fixed-object commit and cancellation contract"));
    }
    D.bValid=true;return D;
}
const FAetherControlledActionCatalog& FAetherControlledActionCatalog::Get()
{
    static const FAetherControlledActionCatalog Value=[]
    {
        FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Actions.json"))))
        {FAetherControlledActionCatalog Missing;Missing.Error=TEXT("Missing Actions.json; no fallback actions available");return Missing;}
        return Parse(Json);
    }();return Value;
}
const TArray<FAetherControlledActionDefinition>& AetherControlledActions::All()
{return FAetherControlledActionCatalog::Get().Actions;}
const FAetherControlledActionDefinition* AetherControlledActions::Find(FName Id)
{return All().FindByPredicate([&](const auto& D){return D.ActionId==Id;});}
