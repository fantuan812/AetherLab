#include "Interaction/AetherSceneServiceHandlers.h"
#include "Framework/AetherFrontier.h"
#include "Definitions/AetherV10Definitions.h"
#include "Networking/AetherCommandRuntime.h"
#include "World/AetherWorldCapability.h"
#include "ReactiveWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
FString FAetherSceneServiceHandlers::DrawWater(const FAetherSceneServiceContext& X)
{
    auto& M=X.Mode;auto* C=&X.Character;auto* PS=C->ProfileState();const auto* P=PS->GetNativeProfile();
    auto* Target=&X.Target;const auto* Action=&X.Action;const auto& D=FAetherV10Definitions::Get();
    auto* Reactive=M.GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();using K=EAetherInteractionActionKind;

        if(!Reactive)return TEXT("供水机制不可用。");
        const double Kg=Reactive->WithdrawWater(Target->Reactive,FMath::Max(0.f,3-C->WaterReserveKg));C->WaterReserveKg+=Kg;
        return FString::Printf(TEXT("从有限水源取水 %.2f 千克。"),Kg);

}

FString FAetherSceneServiceHandlers::PourWater(const FAetherSceneServiceContext& X)
{
    auto& M=X.Mode;auto* C=&X.Character;auto* PS=C->ProfileState();const auto* P=PS->GetNativeProfile();
    auto* Target=&X.Target;const auto* Action=&X.Action;const auto& D=FAetherV10Definitions::Get();
    auto* Reactive=M.GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();using K=EAetherInteractionActionKind;

        auto* Body=AetherCapabilities::WaterReceiver(C,Target,D.Rules.PourRangeCm);
        if(!Body||!Reactive)return TEXT("没有可接收水且通路畅通的目标。");
        const double Kg=Reactive->TransferWater(Target->Reactive,Body,D.Rules.PourKg,C);
        return FString::Printf(TEXT("已转移 %.3f 千克水；未被接收的水仍保留在桶内。"),Kg);

}

FString FAetherSceneServiceHandlers::Gate(const FAetherSceneServiceContext& X)
{
    auto& M=X.Mode;auto* C=&X.Character;auto* PS=C->ProfileState();const auto* P=PS->GetNativeProfile();
    auto* Target=&X.Target;const auto* Action=&X.Action;const auto& D=FAetherV10Definitions::Get();
    auto* Reactive=M.GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();using K=EAetherInteractionActionKind;
if(!Target->Mechanism)return TEXT("机关不可用。");
        Target->Mechanism->bGateOpen=Action->Kind==K::OpenGate;Target->ForceNetUpdate();
        return TEXT("已设置门的目标状态；实际运动仍受动力和障碍物约束。");
}
