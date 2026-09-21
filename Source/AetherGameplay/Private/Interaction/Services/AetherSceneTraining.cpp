#include "Interaction/AetherSceneServiceHandlers.h"
#include "Framework/AetherFrontier.h"
#include "Definitions/AetherV10Definitions.h"
#include "Networking/AetherCommandRuntime.h"
#include "World/AetherWorldCapability.h"
#include "ReactiveWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
FString FAetherSceneServiceHandlers::Train(const FAetherSceneServiceContext& X)
{
    auto& M=X.Mode;auto* C=&X.Character;auto* PS=C->ProfileState();const auto* P=PS->GetNativeProfile();
    auto* Target=&X.Target;const auto* Action=&X.Action;const auto& D=FAetherV10Definitions::Get();
    auto* Reactive=M.GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();using K=EAetherInteractionActionKind;

        if(!P->Claims.Contains(TEXT("Q_Main_02")))return TEXT("请先完成登记和旅舍绑定。");
        // 个人训练实体在同一生命中唯一；重复对话不能重新点燃已熄灭火点。
        if(!P->Claims.Contains(TEXT("Q_Main_03")))
        {
            const FName Id(*(TEXT("Training_")+P->CharacterId));
            if(!P->Evidence.Contains(TEXT("TrainingExtinguished"))&&!M.Prop(Id))
            {
                auto* Fire=M.Make(Id,"TrainingExtinguished",C->GetActorLocation()+FVector(220,0,-50),{.6,.6,.6},EAetherObjectKind::Timber,TEXT("个人训练：使用引泉灭火"));
                if(!Fire)return TEXT("训练场景暂时无法创建。");
                Fire->SetOwner(C);FReactiveStimulus Heat;Heat.HeatJ=80000;Fire->Reactive->Inject(Heat);
            }
            const FName Tag(*(TEXT("Trainer_")+P->CharacterId));bool Exists=false;
            for(TActorIterator<AAetherFrontierCharacter> It(M.GetWorld());It;++It)if(It->Tags.Contains(Tag)&&It->Alive())Exists=true;
            if(!Exists)
            {
                auto* Guard=M.SpawnFighter(C->GetActorLocation()+FVector(300,200,0),EAetherFighter::ShieldGuard,NAME_None);
                if(!Guard)return TEXT("训练对手暂时无法创建。");
                Guard->Tags.Add(Tag);Guard->SetOwner(C);Guard->Reactive->bOwnerOnlyStimuli=true;Guard->SetVitals(40,100,100);
            }
        }
        return TEXT("可以练习攻击木桩、格挡和引泉灭火；已完成的个人训练不会重置。");

}

FString FAetherSceneServiceHandlers::BeginDaily(const FAetherSceneServiceContext& X)
{
    auto& M=X.Mode;auto* C=&X.Character;auto* PS=C->ProfileState();const auto* P=PS->GetNativeProfile();
    auto* Target=&X.Target;const auto* Action=&X.Action;const auto& D=FAetherV10Definitions::Get();
    auto* Reactive=M.GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();using K=EAetherInteractionActionKind;

        const auto* Daily=D.Rules.Dailies.FindByPredicate([&](const auto& V){return V.Service==Target->Service;});
        if(!Daily||!P->Claims.Contains(Daily->QuestGate.ToString()))return TEXT("先完成主线以解锁委托。");
        const FString Day=FDateTime::UtcNow().ToString(TEXT("%Y%m%d"));
        if(P->DailyDate>Day)return TEXT("服务器日期暂未追上已记录日期。");
        if(P->DailyDate==Day&&P->DailyClaims.Contains(Daily->Id.ToString()))return TEXT("本日委托已完成。");
        if(Daily->bPersonalFires)for(int32 I=0;I<Daily->Facts.Num();++I)
        {
            if(P->DailyDate==Day&&P->DailyEvidence.Contains(Daily->Facts[I].ToString()))continue;
            const FName Id(*FString::Printf(TEXT("Commission_%s_%s_%d"),*P->CharacterId,*Day,I));
            if(M.Prop(Id))continue;
            auto* Fire=M.Make(Id,Daily->Facts[I],{-27000.f+I*250,1400,50},{.8,.8,1},EAetherObjectKind::Timber,TEXT("个人委托：灭火"));
            if(!Fire)return TEXT("部分委托场景尚未就绪，请稍后再次开始。");
            Fire->SetOwner(C);FReactiveStimulus Heat;Heat.HeatJ=60000;Fire->Reactive->Inject(Heat);
        }
        return Daily->bPersonalFires?TEXT("前往灰木区熄灭自己的三处委托火点，然后回来领取奖励。"):TEXT("按委托要求准备补给或完成三处巡逻，再回来领取奖励。");

}
