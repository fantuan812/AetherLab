#include "AetherFrontier.h"
#include "Framework/AetherPlayerController.h"
#include "Interaction/AetherNativeInteraction.h"
#include "Networking/AetherCommandRuntime.h"
#include "Definitions/AetherV10Definitions.h"
#include "Inventory/AetherResourceGate.h"
#include "AetherGuide.h"
#include "AetherActions.h"
#include "AetherWorldCapability.h"
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
    auto* Reactive=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();
    switch(Action->Kind)
    {
    case K::Rest:
        if(!P->bRegistered||X.SafeForSeconds<8)return TEXT("绑定旅舍且脱离战斗八秒后才能休息。");
        C->SetVitals(C->MaxHealth,C->MaximumMana(),C->MaximumStamina());C->WaterReserveKg=3;
        return TEXT("已在旅舍休息，恢复生命、法力、体力和随身储水。");
    case K::Trade:case K::Repair:
        return C->OpenTrade(Target)?TEXT("服务已开启，请在背包中查看商品或修理装备。"):TEXT("暂时无法交易，请靠近商人并保持安全。");
    case K::Train:
    {
        if(!P->Claims.Contains(TEXT("Q_Main_02")))return TEXT("请先完成登记和旅舍绑定。");
        // 个人训练实体在同一生命中唯一；重复对话不能重新点燃已熄灭火点。
        if(!P->Claims.Contains(TEXT("Q_Main_03")))
        {
            const FName Id(*(TEXT("Training_")+P->CharacterId));
            if(!P->Evidence.Contains(TEXT("TrainingExtinguished"))&&!Prop(Id))
            {
                auto* Fire=Make(Id,"TrainingExtinguished",C->GetActorLocation()+FVector(220,0,-50),{.6,.6,.6},EAetherObjectKind::Timber,TEXT("个人训练：使用引泉灭火"));
                if(!Fire)return TEXT("训练场景暂时无法创建。");
                Fire->SetOwner(C);FReactiveStimulus Heat;Heat.HeatJ=80000;Fire->Reactive->Inject(Heat);
            }
            const FName Tag(*(TEXT("Trainer_")+P->CharacterId));bool Exists=false;
            for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(It->Tags.Contains(Tag)&&It->Alive())Exists=true;
            if(!Exists)
            {
                auto* Guard=SpawnFighter(C->GetActorLocation()+FVector(300,200,0),EAetherFighter::ShieldGuard,NAME_None);
                if(!Guard)return TEXT("训练对手暂时无法创建。");
                Guard->Tags.Add(Tag);Guard->SetOwner(C);Guard->Reactive->bOwnerOnlyStimuli=true;Guard->SetVitals(40,100,100);
            }
        }
        return TEXT("可以练习攻击木桩、格挡和引泉灭火；已完成的个人训练不会重置。");
    }
    case K::BeginDaily:
    {
        const auto* Daily=D.Rules.Dailies.FindByPredicate([&](const auto& V){return V.Service==Target->Service;});
        if(!Daily||!P->Claims.Contains(Daily->QuestGate.ToString()))return TEXT("先完成主线以解锁委托。");
        const FString Day=FDateTime::UtcNow().ToString(TEXT("%Y%m%d"));
        if(P->DailyDate>Day)return TEXT("服务器日期暂未追上已记录日期。");
        if(P->DailyDate==Day&&P->DailyClaims.Contains(Daily->Id.ToString()))return TEXT("本日委托已完成。");
        if(Daily->bPersonalFires)for(int32 I=0;I<Daily->Facts.Num();++I)
        {
            if(P->DailyDate==Day&&P->DailyEvidence.Contains(Daily->Facts[I].ToString()))continue;
            const FName Id(*FString::Printf(TEXT("Commission_%s_%s_%d"),*P->CharacterId,*Day,I));
            if(Prop(Id))continue;
            auto* Fire=Make(Id,Daily->Facts[I],{-27000.f+I*250,1400,50},{.8,.8,1},EAetherObjectKind::Timber,TEXT("个人委托：灭火"));
            if(!Fire)return TEXT("部分委托场景尚未就绪，请稍后再次开始。");
            Fire->SetOwner(C);FReactiveStimulus Heat;Heat.HeatJ=60000;Fire->Reactive->Inject(Heat);
        }
        return Daily->bPersonalFires?TEXT("前往灰木区熄灭自己的三处委托火点，然后回来领取奖励。"):TEXT("按委托要求准备补给或完成三处巡逻，再回来领取奖励。");
    }
    case K::DrawWater:
    {
        if(!Reactive)return TEXT("供水机制不可用。");
        const double Kg=Reactive->WithdrawWater(Target->Reactive,FMath::Max(0.f,3-C->WaterReserveKg));C->WaterReserveKg+=Kg;
        return FString::Printf(TEXT("从有限水源取水 %.2f 千克。"),Kg);
    }
    case K::PourWater:
    {
        auto* Body=AetherCapabilities::WaterReceiver(C,Target,D.Rules.PourRangeCm);
        if(!Body||!Reactive)return TEXT("没有可接收水且通路畅通的目标。");
        const double Kg=Reactive->TransferWater(Target->Reactive,Body,D.Rules.PourKg,C);
        return FString::Printf(TEXT("已转移 %.3f 千克水；未被接收的水仍保留在桶内。"),Kg);
    }
    case K::OpenGate:case K::CloseGate:
        if(!Target->Mechanism)return TEXT("机关不可用。");
        Target->Mechanism->bGateOpen=Action->Kind==K::OpenGate;Target->ForceNetUpdate();
        return TEXT("已设置门的目标状态；实际运动仍受动力和障碍物约束。");
    case K::StartEncounter:
        if(!Encounters)return TEXT("遭遇服务尚未就绪。");
        return Encounters->Start(C,Target->Service=="Activity");
    case K::ChannelEncounter:
        return Encounters?Encounters->Channel(C):TEXT("遭遇服务尚未就绪。");
    case K::CollectLegacyLoot:
        return ClaimNativeLegacyLoot(C,Target->Spec.Id);
    case K::RecruitGuard:case K::RecruitHealer:
    {
        if(!CanChangeParty(C))return TEXT("仅在安全区域且未参与遭遇时招募。");
        Companions.RemoveAll([](const auto& B){return !IsValid(B);});
        const bool Healer=Action->Kind==K::RecruitHealer;const FName Id=Healer?FName("Muhe"):FName("Lishi");
        for(const auto& B:Companions)if(B->CompanionId==Id)return TEXT("这位同伴已在队伍中。");
        if(Companions.Num()+GetNumPlayers()>=4)return TEXT("真人和同伴合计最多四人。");
        auto* B=SpawnFighter(C->GetActorLocation()+FVector(0,150,20),EAetherFighter::Player,NAME_None);
        if(!B)return TEXT("无法创建同伴。");
        B->CompanionOwner=C;B->bHealer=Healer;B->CompanionId=Id;
        // 在可回滚的实体装配之后接受服务器事实；任务奖励只在其事务提交后发布。
        FAetherServerFact Fact;Fact.Kind=EAetherServerFactKind::Personal;Fact.CharacterId=P->CharacterId;Fact.FactId=TEXT("Companion");
        FString Why;
        if(!GetGameInstance()->GetSubsystem<UAetherCommandRuntime>()->ObserveServerFact(MoveTemp(Fact),Why))
        {B->Destroy();return TEXT("进度队列繁忙，本次招募已取消。");}
        B->SpawnDefaultController();Companions.Add(B);
        return TEXT("同伴已加入当前队伍；个人招募进度正在保存。");
    }
    default:return TEXT("服务不可用。");
    }
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
    for(auto* Buddy:Companions)if(IsValid(Buddy)&&Buddy->CompanionOwner==Pawn)Buddy->Destroy();
    Companions.RemoveAll([](const auto& B){return !IsValid(B);});
}
