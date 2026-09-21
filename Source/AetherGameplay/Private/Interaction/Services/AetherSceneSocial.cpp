#include "Interaction/AetherSceneServiceHandlers.h"
#include "Framework/AetherFrontier.h"
#include "Definitions/AetherV10Definitions.h"
#include "Networking/AetherCommandRuntime.h"
#include "World/AetherWorldCapability.h"
#include "ReactiveWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
FString FAetherSceneServiceHandlers::Rest(const FAetherSceneServiceContext& X)
{
    auto& M=X.Mode;auto* C=&X.Character;auto* PS=C->ProfileState();const auto* P=PS->GetNativeProfile();
    auto* Target=&X.Target;const auto* Action=&X.Action;const auto& D=FAetherV10Definitions::Get();
    auto* Reactive=M.GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();using K=EAetherInteractionActionKind;
if(!P->bRegistered||X.SafeForSeconds<8)return TEXT("绑定旅舍且脱离战斗八秒后才能休息。");
        C->SetVitals(C->MaxHealth,C->MaximumMana(),C->MaximumStamina());C->WaterReserveKg=3;
        {FString Why;if(!PS->GrantRestBlessing(Why))return TEXT("资源已恢复，祝福暂不可用：")+Why;}
        return TEXT("已恢复资源；学会引泉后，额外获得五分钟引泉强化与流水护佑。");
}

FString FAetherSceneServiceHandlers::Trade(const FAetherSceneServiceContext& X)
{
    auto& M=X.Mode;auto* C=&X.Character;auto* PS=C->ProfileState();const auto* P=PS->GetNativeProfile();
    auto* Target=&X.Target;const auto* Action=&X.Action;const auto& D=FAetherV10Definitions::Get();
    auto* Reactive=M.GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();using K=EAetherInteractionActionKind;
return C->OpenTrade(Target)?TEXT("服务已开启，请在背包中查看商品或修理装备。"):TEXT("暂时无法交易，请靠近商人并保持安全。");
}

FString FAetherSceneServiceHandlers::Recruit(const FAetherSceneServiceContext& X)
{
    auto& M=X.Mode;auto* C=&X.Character;auto* PS=C->ProfileState();const auto* P=PS->GetNativeProfile();
    auto* Target=&X.Target;const auto* Action=&X.Action;const auto& D=FAetherV10Definitions::Get();
    auto* Reactive=M.GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();using K=EAetherInteractionActionKind;

        if(!M.CanChangeParty(C))return TEXT("仅在安全区域且未参与遭遇时招募。");
        M.Companions.RemoveAll([](const auto& B){return !IsValid(B);});
        const bool Healer=Action->Kind==K::RecruitHealer;const FName Id=Healer?FName("Muhe"):FName("Lishi");
        for(const auto& B:M.Companions)if(B->CompanionId==Id)return TEXT("这位同伴已在队伍中。");
        if(M.PartySize(PS->PartyLeader)>=4)return TEXT("真人和同伴合计最多四人。");
        auto* B=M.SpawnFighter(C->GetActorLocation()+FVector(0,150,20),EAetherFighter::Player,NAME_None);
        if(!B)return TEXT("无法创建同伴。");
        B->CompanionOwner=C;B->bHealer=Healer;B->CompanionId=Id;
        // 在可回滚的实体装配之后接受服务器事实；任务奖励只在其事务提交后发布。
        FAetherServerFact Fact;Fact.Kind=EAetherServerFactKind::Personal;Fact.CharacterId=P->CharacterId;Fact.FactId=TEXT("Companion");
        FString Why;
        if(!M.GetGameInstance()->GetSubsystem<UAetherCommandRuntime>()->ObserveServerFact(MoveTemp(Fact),Why))
        {B->Destroy();return TEXT("进度队列繁忙，本次招募已取消。");}
        B->SpawnDefaultController();M.Companions.Add(B);
        return TEXT("同伴已加入当前队伍；个人招募进度正在保存。");

}
