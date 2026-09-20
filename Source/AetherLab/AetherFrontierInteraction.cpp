#include "AetherFrontier.h"
#include "Inventory/AetherResourceGate.h"
#include "AetherGuide.h"
#include "AetherRules.h"
#include "AetherActions.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

FString AAetherFrontierMode::RecruitCompanion(AAetherFrontierCharacter* C,bool Healer)
{
    auto* PS=C?C->ProfileState():nullptr;if(!PS||(!PS->Profile.Available("Q_Main_06")&&!PS->Profile.Claims.Contains(FName("Q_Main_06"))))return TEXT("Complete both field quests first.");
    if(FVector::DistSquared(C->GetActorLocation(),Prop("Recruit")->GetActorLocation())>FMath::Square(260.0))return TEXT("Recruit at the inn assembly point.");
    if(!CanChangeParty(C))return TEXT("Recruit only out of combat and outside active encounters.");
    Companions.RemoveAll([](const auto& B){return !IsValid(B);});
    bool GuardTaken=false,HealerTaken=false;for(const auto& B:Companions)if(IsValid(B)){GuardTaken|=B->CompanionId=="Lishi";HealerTaken|=B->CompanionId=="Muhe";}
    if(GuardTaken&&HealerTaken)return TEXT("Both named companions are already recruited.");
    Healer=GuardTaken||(!HealerTaken&&Healer);
    if(Companions.Num()+GetNumPlayers()>=4)return TEXT("Party capacity: four humans and AI combined.");
    auto* B=SpawnFighter(C->GetActorLocation()+FVector(0,150,20),EAetherFighter::Player,NAME_None);B->CompanionOwner=C;B->bHealer=Healer;B->CompanionId=Healer?FName("Muhe"):FName("Lishi");
    B->SpawnDefaultController();Companions.Add(B);auto Next=PS->Profile;Next.bCompanion=true;Next.Observe("Companion");Next.TryAutoClaim("Q_Main_06");
    if(!Commit(PS,Next)){B->Destroy();return TEXT("Storage unavailable; recruitment cancelled.");}
    return TEXT("Companion follows, fights, and revives. P shows party.");
}
FString AAetherFrontierMode::Interact(AAetherFrontierCharacter* C)
{
    auto* PS=C?C->ProfileState():nullptr;if(!PS||!C->Alive()||C->bTravelPending)return TEXT("Cannot interact.");
    return InteractTarget(C,AetherGuide::SelectInteraction(C));
}
FString AAetherFrontierMode::InteractTarget(AAetherFrontierCharacter* C,const FAetherInteractionTarget& Target)
{
    auto* PS=IsValid(C)?C->ProfileState():nullptr;
    if(!HasAuthority()||!PS||C->ResourceGate->IsBlocked()||C->GetWorld()!=GetWorld()||!AetherGuide::ValidateSelection(C,Target))
        return TEXT("目标、动作或角色进度已变化，请重新交互。");
    if(auto* Downed=Target.Rescue.Get())
    {C->ReviveTarget=Downed;C->ReviveStarted=C->CombatTime();C->ReviveDamageSerial=C->DamageReceivedCount;if(!C->AbilitySystem->TryActivateAbilityByClass(UAetherReviveAbility::StaticClass())){C->ReviveTarget=nullptr;return TEXT("无法开始救援，请靠近队友并保持安全。");}return TEXT("正在救援：保持靠近 3 秒，受伤会打断。");}
    auto* Nearest=Target.Prop.Get();
    if(!Nearest)return TEXT("Move within 2.5m of an interaction marker.");
    const FName Service=Nearest->Service;auto Next=PS->Profile;Next.RefreshDaily(FDateTime::UtcNow().ToString(TEXT("%Y%m%d")));auto* State=GetGameState<AAetherFrontierState>();
    if(AetherGuide::IsPersonalFire(Service))return TEXT("使用引泉实际熄灭自己的火盆；交互不会增加灭火进度。");
    if(Nearest->bInspectableFire&&!AetherGuide::CanInspectFire(Nearest))return TEXT("火点仍未安全清理，请使用引泉或旁边水桶灭火。");
    if(const auto* Required=FAetherRules::Get().InteractionRequirements.Find(Service))for(auto Id:*Required)if(!AetherGuide::CanInspectFire(Prop(Id)))return TEXT("先清理此服务定义要求的火点，再进行交互。");
    if(Service=="Loot")return ClaimLoot(C,Nearest->Spec.Id);
    if(Service=="SupplyA"||Service=="SupplyB")
    {
        if(!Next.Available("Q_Main_01")||Next.Evidence.Contains(Service))return TEXT("Already collected for this character.");
        if(!AetherItems::GrantTable(Next,"Supply",FAetherRules::Get()))return TEXT("Inventory full.");Next.Observe(Service);Next.TryAutoClaim("Q_Main_01");
    }
    else if(Service=="Teacher")
    {
        if(!Next.Claims.Contains(FName("Q_Main_02")))return TEXT("Register and bind the inn first.");
        for(const auto& Unlock:FAetherRules::Get().SpellUnlocks)if(Next.Claims.Contains(Unlock.Key))Next.LearnedSpells|=Unlock.Value;
        if(!Commit(PS,Next))return TEXT("Storage unavailable; try again.");
        if(Next.Available("Q_Main_03"))
        {
            FName Id=*FString(TEXT("Training_" )+Next.CharacterId);auto* Fire=Prop(Id);
            if(!Fire)Fire=Make(Id,"TrainingExtinguished",C->GetActorLocation()+FVector(220,0,-50),{.6,.6,.6},EAetherObjectKind::Timber,TEXT("PERSONAL FIRE / 2 THEN MIDDLE MOUSE TO EXTINGUISH"));
            Fire->SetOwner(C);FReactiveStimulus H;H.HeatJ=80000;Fire->Reactive->Inject(H);
            const FName Tag=*FString(TEXT("Trainer_")+Next.CharacterId);bool Exists=false;
            for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(It->Tags.Contains(Tag)&&It->Alive())Exists=true;
            if(!Exists){auto* Guard=SpawnFighter(C->GetActorLocation()+FVector(300,200,0),EAetherFighter::ShieldGuard,NAME_None);Guard->Tags.Add(Tag);Guard->SetOwner(C);Guard->Reactive->bOwnerOnlyStimuli=true;Guard->SetVitals(40,100,100);}
        }
        return TEXT("Abilities learned. R equip sword. Hit dummy 3 times, block trainer, extinguish your fire.");
    }
    else if(Service=="Inn")
    {
        if(!Next.Evidence.Contains("Register"))return TEXT("Speak to registrar first.");
        Next.Observe("Inn");Next.TryAutoClaim("Q_Main_02");
        if(!Commit(PS,Next))return TEXT("Storage unavailable.");
        if(C->TimeSinceDamage()<8)return TEXT("Rest requires eight seconds out of combat.");
        C->SetVitals(C->MaxHealth,100,100);C->WaterReserveKg=3;return TEXT("Checkpoint bound. Weapons issued once; R equips. Rested.");
    }
    else if(FAetherRules::Get().Shops.Contains(Service))
    {return C->OpenTrade(Nearest)?TEXT("交易已开启，只显示这位商人的商品。"):TEXT("当前无法交易，请保持安全并靠近商人。");}
    else if(FAetherRules::Get().Dailies.ContainsByPredicate([&](const auto& D){return D.Service==Service;}))
    {
        const int32 Template=FAetherRules::Get().Dailies.IndexOfByPredicate([&](const auto& D){return D.Service==Service;});
        const auto& Daily=FAetherRules::Get().Dailies[Template];
        if(!Next.Claims.Contains(Daily.QuestGate))return TEXT("Finish the required story to unlock commissions.");
        if(!Next.ClaimDaily(Template))
        {
            if(Daily.bPersonalFires&&!Next.DailyClaims.Contains(Daily.Id))
            {
                for(int32 I=0;I<Daily.Facts.Num();++I)
                {
                    FName Id=*FString::Printf(TEXT("Commission_%s_%s_%d"),*Next.CharacterId,*Next.DailyDate,I);
                    if(!Prop(Id)){auto* Fire=Make(Id,Daily.Facts[I],{-27000.f+I*250,1400,50},{.8,.8,1},EAetherObjectKind::Timber,TEXT("COMMISSION / EXTINGUISH"));Fire->SetOwner(C);FReactiveStimulus H;H.HeatJ=60000;Fire->Reactive->Inject(H);}
                }
            }
            return Template==0?TEXT("Bring two supplies; one reward per UTC day."):Template==1?TEXT("Visit the three patrol markers, then return."):TEXT("Extinguish your three commission fires in Ashwood, then return.");
        }
    }
    else if(Service.ToString().StartsWith("Patrol"))
    {if(!Next.Claims.Contains(FName("Q_Main_08")))return TEXT("Patrol unlocks after the story.");Next.DailyEvidence.AddUnique(Service);}
    else if(Service=="Gather")
    {
        if(Next.DailyEvidence.Contains(Nearest->Spec.Id))return TEXT("This supply cache was collected today.");
        if(!AetherItems::GrantTable(Next,"Gather",FAetherRules::Get()))return TEXT("Inventory full.");Next.DailyEvidence.Add(Nearest->Spec.Id);
    }
    else if(Service=="Recruit")return RecruitCompanion(C,false);
    else if(Service=="Well")
    {
        const double Water=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->WithdrawWater(Nearest->Reactive,FMath::Max(0.f,3-C->WaterReserveKg));C->WaterReserveKg+=Water;
        return FString::Printf(TEXT("Transferred %.2f kg. This reservoir is finite."),Water);
    }
    else if(Service=="Bucket")
    {
        auto* Body=AetherCapabilities::WaterReceiver(C,Nearest,FAetherRules::Get().PourRangeCm);
        auto* Receiver=Body?Cast<AAetherFrontierProp>(Body->GetOwner()):nullptr;
        if(!Body)return TEXT("附近没有可接收水的目标，或通路被遮挡。");
        if(Receiver&&Receiver->bInspectableFire&&AetherGuide::CanInspectFire(Receiver))
        {Observe(C,Receiver->Service,Receiver->Spec.Id);return TEXT("已检查清理后的火点，未额外消耗桶中水。");}
        const double Water=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->TransferWater(Nearest->Reactive,Body,FAetherRules::Get().PourKg,C);
        return Water>0?FString::Printf(TEXT("已转移 %.3f kg 水；未被接收的水保留在桶中。"),Water):TEXT("无法转移：水源无液态水、目标已满或通路受阻。");
    }
    else if(Service=="HingedGate"){Nearest->Mechanism->bGateOpen=!Nearest->Mechanism->bGateOpen;return TEXT("Gate motor toggled; physical obstructions resist its limited force.");}
    else if(AetherServices::IsService(Service))
    {
        FAetherWorldServiceCommand Command;Command.Id=FGuid::NewGuid();Command.TargetId=Nearest->Spec.Id;Command.ExpectedRevision=PS->Profile.Revision;
        return AetherServices::Message(ExecuteWorldService(C,Command,&Target));
    }
    else if(Service=="Abbey")
    {
        if(!Next.Available("Q_Main_07")&&!Next.Claims.Contains(FName("Q_Main_07")))return TEXT("Complete both field quests and recruit first.");
        return Encounters?Encounters->Start(C,false):TEXT("Encounter unavailable.");
    }
    else if(Service=="AbbeyValve")return Encounters?Encounters->Channel(C):TEXT("Encounter unavailable.");
    else if(Service=="GuardianDefeated")
    {
        if(!IsValid(Guardian)||Guardian->Alive()||!KillCredit.FindRef(Guardian).Contains(*Next.CharacterId))return TEXT("No completed encounter participation.");
        Next.Observe(Service);Next.TryAutoClaim("Q_Main_07");
    }
    else if(Service=="Activity")
    {
        if(!Next.Claims.Contains(FName("Q_Main_08")))return TEXT("Public defence unlocks after the main story.");
        if(!Encounters)return TEXT("Encounter unavailable.");
        if(Encounters->Relay.Phase==EAetherEncounterPhase::Channel)return Encounters->Channel(C);
        return Encounters->Start(C,true);
    }
    else if(Service=="SealDelivered")
    {
        if(!Next.Available("Q_Main_08")||Next.Count("AncientSeal")<1)return TEXT("Bring the ancient seal from your completed encounter.");
        Next.Observe(Service);Next.TryAutoClaim("Q_Main_08");
    }
    else {if(!Next.Observe(Service)&&Service!="Rescue")return TEXT("No new objective here; J shows current requirements.");AetherQuests::Settle(Next,Database->WorldFacts,false);}
    if(!Commit(PS,Next,Service=="Rescue"?FName("Rescue"):NAME_None,Service=="Rescue"?Nearest->Spec.Id:NAME_None))return TEXT("Storage unavailable; no inventory or reward change committed. Retry.");
    return TEXT("Interaction committed. J: quests / I: inventory / K: abilities.");
}
