#include "Interaction/AetherSceneServiceHandlers.h"
#include "Framework/AetherFrontier.h"
#include "Definitions/AetherV10Definitions.h"
#include "Networking/AetherCommandRuntime.h"
#include "World/AetherWorldCapability.h"
#include "ReactiveWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
FString FAetherSceneServiceHandlers::StartEncounter(const FAetherSceneServiceContext& X)
{
    auto& M=X.Mode;auto* C=&X.Character;auto* PS=C->ProfileState();const auto* P=PS->GetNativeProfile();
    auto* Target=&X.Target;const auto* Action=&X.Action;const auto& D=FAetherV10Definitions::Get();
    auto* Reactive=M.GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();using K=EAetherInteractionActionKind;
if(!M.Encounters)return TEXT("遭遇服务尚未就绪。");
        return M.Encounters->Start(C,Target->Service=="Activity");
}

FString FAetherSceneServiceHandlers::ChannelEncounter(const FAetherSceneServiceContext& X)
{
    auto& M=X.Mode;auto* C=&X.Character;auto* PS=C->ProfileState();const auto* P=PS->GetNativeProfile();
    auto* Target=&X.Target;const auto* Action=&X.Action;const auto& D=FAetherV10Definitions::Get();
    auto* Reactive=M.GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();using K=EAetherInteractionActionKind;
return M.Encounters?M.Encounters->Channel(C):TEXT("遭遇服务尚未就绪。");
}

FString FAetherSceneServiceHandlers::CollectLegacyLoot(const FAetherSceneServiceContext& X)
{
    auto& M=X.Mode;auto* C=&X.Character;auto* PS=C->ProfileState();const auto* P=PS->GetNativeProfile();
    auto* Target=&X.Target;const auto* Action=&X.Action;const auto& D=FAetherV10Definitions::Get();
    auto* Reactive=M.GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();using K=EAetherInteractionActionKind;
return M.ClaimNativeLegacyLoot(C,Target->Spec.Id);
}
