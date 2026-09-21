#include "Framework/AetherFrontier.h"
#include "Interaction/AetherSceneServiceHandlers.h"
#include "Framework/AetherPlayerController.h"
#include "Interaction/AetherNativeInteraction.h"
#include "Networking/AetherCommandRuntime.h"
#include "Definitions/AetherV10Definitions.h"
#include "Inventory/AetherResourceGate.h"
#include "Quests/AetherGuide.h"
#include "Interaction/AetherActions.h"
#include "World/AetherWorldCapability.h"
#include "ReactiveWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"

FString AAetherFrontierMode::ExecuteNativeSceneService(AAetherPlayerController& PC,const FAetherPlayerCommand& Command)
{
    using K=EAetherInteractionActionKind;
    auto* C=Cast<AAetherFrontierCharacter>(PC.GetPawn());auto* PS=PC.GetPlayerState<AAetherPlayerState>();
    const auto* P=PS?PS->GetNativeProfile():nullptr;const auto& D=FAetherV10Definitions::Get();
    FAetherProfileCommandContext X;
    if(!bNativeMode||!NativeSceneReady()||!C||!P||!D.bValid||
        !ResolveNativeContext(PC,Command,*P,X))return TEXT("角色或场景尚未就绪。");
    auto* Target=Prop(FName(*Command.TargetStableId));const auto* Definition=D.Interactions.Targets.Find(X.Interaction.DefinitionId);
    if(!Target||!Definition)return TEXT("目标已离开当前区域。");
    for(const auto& A:Definition->Actions)
        if(AetherNativeInteraction::IsPersistent(A.Kind)||AetherNativeInteraction::IsSceneService(A.Kind))
            X.Interaction.RegisteredHandlers.Add(A.Kind);
    FAetherInteractionProvider Provider(*Definition,X.Interaction,D.Rules);
    if(Provider.CheckCommand(P->CharacterId,Command)!=EAetherCommandCode::Applied)return TEXT("目标、条件或版本已变化，请重新选择。");
    const auto* Action=Definition->Actions.FindByPredicate([&](const auto& A){return A.Id.Equals(Command.ActionId,ESearchCase::CaseSensitive);});
    if(!Action||!AetherNativeInteraction::IsSceneService(Action->Kind))return TEXT("此动作不属于现场服务。");
    if(!X.bServiceRequirementsMet)return TEXT("请先完成此服务要求的现场条件。");
    return FAetherSceneServiceHandlers::Execute({*this,*C,*Target,*Action,X.SafeForSeconds});
}

void AAetherFrontierMode::ReleaseNativePawn(AAetherFrontierCharacter* Pawn)
{
    if(!bNativeMode||!Pawn||!Pawn->ProfileState())return;
    // 私人训练属于本次 Pawn 生命；残留实体不能挡住新生命重建同名个人目标。
    for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)
        if(*It!=Pawn&&It->Reactive->bOwnerOnlyStimuli&&It->GetOwner()==Pawn)It->Destroy();
    for(int32 I=Props.Num()-1;I>=0;--I)
        if(IsValid(Props[I])&&Props[I]->GetOwner()==Pawn&&Props[I]->Reactive->StableId.IsNone())
        {Registry.Remove(Props[I]->Spec.Id);Props[I]->Destroy();Props.RemoveAt(I);}
    for(const auto& Buddy:Companions)if(IsValid(Buddy)&&Buddy->CompanionOwner==Pawn)Buddy->Destroy();
    Companions.RemoveAll([](const auto& B){return !IsValid(B);});
}
