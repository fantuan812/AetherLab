#include "Validation/AetherValidateV10ContentCommandlet.h"
#include "Definitions/AetherV10Definitions.h"
#include "Assets/AetherContent.h"
#include "Animation/AetherActionPresentation.h"
#include "AetherMotionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Retargeter/IKRetargeter.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
UAetherValidateV10ContentCommandlet::UAetherValidateV10ContentCommandlet(){IsClient=false;IsServer=false;IsEditor=true;LogToConsole=true;}
int32 UAetherValidateV10ContentCommandlet::Main(const FString&)
{
    int32 Errors=0;const auto Check=[&](bool Good,const FString& What)
    {if(!Good){++Errors;UE_LOG(LogTemp,Error,TEXT("V10_CONTENT_FAIL %s"),*What);}};
    Check(FAetherV10Definitions::Get().bValid,TEXT("Canonical definitions"));
    FString Json;TSharedPtr<FJsonObject> Layouts;
    Check(FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/WidgetLayouts.json")))&&
          FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Layouts)&&Layouts.IsValid(),TEXT("Widget layout definitions"));
    if(Layouts.IsValid())for(const auto& Entry:Layouts->GetObjectField(TEXT("Layouts"))->Values)
    {
        const FString Path=FString(TEXT("/Game/UI/Widgets/"))+FString(Entry.Key)+TEXT(".")+FString(Entry.Key);
        auto* Blueprint=LoadObject<UWidgetBlueprint>(nullptr,*Path);
        Check(Blueprint&&Blueprint->Status!=BS_Error&&Blueprint->GeneratedClass&&Blueprint->WidgetTree&&Blueprint->WidgetTree->RootWidget,Path);
        if(!Blueprint||!Blueprint->WidgetTree)continue;
        TFunction<void(const TSharedPtr<FJsonObject>&)> Visit=[&](const auto& Node)
        {
            FString Name=Node->GetStringField(TEXT("Name"));auto* Widget=Blueprint->WidgetTree->FindWidget(FName(*Name));
            Check(Widget!=nullptr,Path+TEXT(" / ")+Name);
            const TArray<TSharedPtr<FJsonValue>>* Children=nullptr;
            if(Node->TryGetArrayField(TEXT("Children"),Children))for(const auto& Child:*Children)Visit(Child->AsObject());
        };
        Visit(Entry.Value->AsObject());
    }
    auto* Actions=LoadObject<UAetherActionSet>(nullptr,TEXT("/Game/Animation/Controlled/DA_Actions.DA_Actions"));
    Check(Actions!=nullptr,TEXT("Controlled action set"));
    if(Actions)for(const TCHAR* Name:{TEXT("Death"),TEXT("GetUp"),TEXT("Stun"),TEXT("Hit"),TEXT("Land"),TEXT("LandHeavy"),TEXT("CrouchIdle"),TEXT("CrouchWalk"),TEXT("CrouchBack"),TEXT("CrouchLeft"),TEXT("CrouchRight"),TEXT("CarryIdle"),TEXT("CarryWalk"),TEXT("Pickup"),TEXT("PutDown"),TEXT("Throw"),TEXT("Rescue"),TEXT("Vault"),TEXT("Cast"),TEXT("Guard"),TEXT("DodgeForward"),TEXT("DodgeBack"),TEXT("DodgeLeft"),TEXT("DodgeRight")})
    {
        const auto* Clip=Actions->Clips.Find(Name);
        Check(Clip&&Clip->Get()&&(*Clip)->GetSkeleton()&&(*Clip)->GetPlayLength()>0,FString(TEXT("Controlled clip: "))+Name);
    }
    for(const TCHAR* Body:{TEXT("Manny"),TEXT("Quinn")})
    {
        const FString Name=TEXT("DA_Motion")+FString(Body),Path=TEXT("/Game/Animation/Motion/")+Name+TEXT(".")+Name;
        auto* P=LoadObject<UAetherMotionProfile>(nullptr,*Path);FString Why;
        Check(P&&P->Validate(Why),Path+TEXT(" ")+Why);if(!P)continue;
        Check(P->SourceMesh.LoadSynchronous()&&P->Retargeter.LoadSynchronous()&&P->SourceAnimationClass.LoadSynchronous(),Path+TEXT(" rig/mesh/AnimBP"));
        for(const TCHAR* Style:{TEXT("Idle"),TEXT("Walk"),TEXT("Combat"),TEXT("StrafeLeft"),TEXT("StrafeRight"),TEXT("Injured"),TEXT("Crouch"),TEXT("CrouchIdle")})
        {Check(P->Styles.Contains(Style),Path+TEXT(" style ")+Style);Check(P->TransitionBoundaries.Contains(Style),Path+TEXT(" boundary ")+Style);}
    }
    auto* Game=LoadObject<UAetherGameContent>(nullptr,TEXT("/Game/AetherCore/Data/DA_GameContent.DA_GameContent"));
    Check(Game&&Game->EquipmentCatalog,TEXT("Game content/equipment catalog"));
    if(Game)for(auto* C:{Game->Player.Get(),Game->Guard.Get(),Game->Caster.Get(),Game->Boss.Get()})
        Check(C&&C->BodyMesh.LoadSynchronous()&&C->AnimationClass.LoadSynchronous(),TEXT("Character body and AnimBP"));
    const TCHAR* Required[]={TEXT("/Game/UI/Materials/M_CharacterPreview.M_CharacterPreview"),TEXT("/Game/UI/DA_UITheme.DA_UITheme"),
        TEXT("/Game/Animation/ABP_AetherCharacter.ABP_AetherCharacter_C"),TEXT("/Game/AetherCore/Maps/L_Frontier.L_Frontier")};
    for(const auto* Path:Required)Check(LoadObject<UObject>(nullptr,Path)!=nullptr,Path);
    UE_LOG(LogTemp,Display,TEXT("V10_CONTENT_RESULT errors=%d"),Errors);return Errors?1:0;
}
