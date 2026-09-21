#include "UI/AetherFrontierHUD.h"
#include "UI/AetherMenuRoot.h"
#include "UI/AetherPlayerHUDWidget.h"
#include "AetherFrontierPanel.h"
#include "Tests/AetherMenuInteractionProbe.h"
#include "Characters/AetherFrontierCharacter.h"
#include "ReactiveWorldSubsystem.h"
#include "Engine/Canvas.h"
void AAetherFrontierHUD::BeginPlay()
{
    Super::BeginPlay();
    if(PlayerOwner&&PlayerOwner->IsLocalController())
    {
        PlayerHUD=CreateWidget<UAetherPlayerHUDWidget>(PlayerOwner);PlayerHUD->AddToViewport(0);
        MenuRoot=CreateWidget<UAetherMenuRoot>(PlayerOwner);MenuRoot->AddToViewport(10);PanelWidget=MenuRoot->GetPanel();
    }
}
void AAetherFrontierHUD::DrawHUD()
{
    Super::DrawHUD();AetherMenuInteraction::Tick(this,PanelWidget);
#if !UE_BUILD_SHIPPING
    // Canvas 只用于显式打开的开发诊断；正式生命、交互、任务和菜单使用 UMG 控件。
    const auto* C=Cast<AAetherFrontierCharacter>(GetOwningPawn());
    if(Canvas&&C&&C->bDebugOverlay)if(const auto* W=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>())
        DrawText(W->GetStatsText(),FLinearColor(.5,1,.65),20,220);
#endif
}
void AAetherFrontierHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    if(MenuRoot)MenuRoot->RemoveFromParent();MenuRoot=nullptr;PanelWidget=nullptr;
    if(PlayerHUD)PlayerHUD->RemoveFromParent();PlayerHUD=nullptr;DialogueWidget=nullptr;
    Super::EndPlay(Reason);
}
