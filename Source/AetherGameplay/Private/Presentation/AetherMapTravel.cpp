#include "Characters/AetherFrontierCharacter.h"
#include "Framework/AetherFrontierMode.h"
#include "Definitions/AetherMapDefinition.h"
#include "Inventory/AetherResourceGate.h"
#include "Movement/AetherTraversal.h"
#include "Framework/AetherFrontier.h"
#include "GameFramework/CharacterMovementComponent.h"
void AAetherFrontierCharacter::ServerMapTravel_Implementation(FName Id,int64 Seen)
{
    if(!HasAuthority()||CombatTime()<NextServerAction)return;NextServerAction=CombatTime()+.5f;
    auto* PS=ProfileState();const auto* P=PS?PS->GetNativeProfile():nullptr;
    auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();const auto& D=FAetherMapDefinitions::Get();
    const auto* B=D.Beacons.FindByPredicate([&](const auto& V){return V.Id==Id;});
    if(!P||P->Revision!=Seen||!M||!M->NativeSceneReady()||!D.bValid||!B||
       (!B->RequiredQuest.IsNone()&&!P->Claims.Contains(B->RequiredQuest.ToString())))
    {Notify(TEXT("据点尚未解锁或资料已变化。"));return;}
    if(!Ready()||bTravelPending||Carried||ReviveTarget||HasRecentCombat(8)||ResourceGate->IsBlocked()||GetCharacterMovement()->IsFalling()||
       (M->Encounters&&M->Encounters->IsChanneling(this)))
    {Notify(TEXT("当前动作或战斗状态不允许传送。"));return;}
    // 目标仅来自服务器规范数据；复用区域就绪、导航和落点检查，UI 从不提供任意坐标。
    BeginSafeTravel(B->Position);
}
