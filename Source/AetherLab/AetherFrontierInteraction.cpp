#include "AetherFrontier.h"
#include "AetherRules.h"
#include "AetherActions.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

FString AAetherFrontierMode::RecruitCompanion(AAetherFrontierCharacter* C,bool Healer)
{
    auto* PS=C?C->ProfileState():nullptr;if(!PS||(!PS->Profile.Available(5)&&!PS->Profile.Claims.Contains(FAetherProfile::QuestId(5))))return TEXT("Complete both field quests first.");
    if(FVector::DistSquared(C->GetActorLocation(),Prop("Recruit")->GetActorLocation())>FMath::Square(260.0))return TEXT("Recruit at the inn assembly point.");
    if(!CanChangeParty(C))return TEXT("Recruit only out of combat and outside active encounters.");
    Companions.RemoveAll([](const auto& B){return !IsValid(B);});
    bool GuardTaken=false,HealerTaken=false;for(const auto& B:Companions)if(IsValid(B)){GuardTaken|=B->CompanionId=="Lishi";HealerTaken|=B->CompanionId=="Muhe";}
    if(GuardTaken&&HealerTaken)return TEXT("Both named companions are already recruited.");
    Healer=GuardTaken||(!HealerTaken&&Healer);
    if(Companions.Num()+GetNumPlayers()>=4)return TEXT("Party capacity: four humans and AI combined.");
    auto* B=SpawnFighter(C->GetActorLocation()+FVector(0,150,20),EAetherFighter::Player,NAME_None);B->CompanionOwner=C;B->bHealer=Healer;B->CompanionId=Healer?FName("Muhe"):FName("Lishi");
    B->SpawnDefaultController();Companions.Add(B);auto Next=PS->Profile;Next.bCompanion=true;Next.Observe("Companion");Next.Claim(5);
    if(!Commit(PS,Next)){B->Destroy();return TEXT("Storage unavailable; recruitment cancelled.");}
    return TEXT("Companion follows, fights, and revives. P shows party.");
}
FString AAetherFrontierMode::Interact(AAetherFrontierCharacter* C)
{
    auto* PS=C?C->ProfileState():nullptr;if(!PS||!C->Alive())return TEXT("Cannot interact.");
    const auto Target=AetherGuide::SelectInteraction(C);
    if(auto* Downed=Target.Rescue.Get())
    {C->ReviveTarget=Downed;C->ReviveStarted=C->CombatTime();C->ReviveDamageSerial=C->DamageReceivedCount;if(!C->AbilitySystem->TryActivateAbilityByClass(UAetherReviveAbility::StaticClass())){C->ReviveTarget=nullptr;return TEXT("无法开始救援，请靠近队友并保持安全。");}return TEXT("正在救援：保持靠近 3 秒，受伤会打断。");}
    auto* Nearest=Target.Prop.Get();
    if(!Nearest)return TEXT("Move within 2.5m of an interaction marker.");
    const FName Service=Nearest->Service;auto Next=PS->Profile;Next.RefreshDaily(FDateTime::UtcNow().ToString(TEXT("%Y%m%d")));auto* State=GetGameState<AAetherFrontierState>();
    if(AetherGuide::IsPersonalFire(Service))return TEXT("使用引泉实际熄灭自己的火盆；交互不会增加灭火进度。");
    if(Service.ToString().StartsWith("ForestFire")&&!AetherGuide::CanInspectFire(Nearest))return TEXT("火点仍未安全清理，请使用引泉或旁边水桶灭火。");
    if(Service=="Rescue")for(int32 I=0;I<3;++I)if(!AetherGuide::CanInspectFire(Prop(*FString::Printf(TEXT("ForestFire%d"),I))))return TEXT("先使三处火点熄灭并冷却，再救援工匠。");
    if(Service=="Loot")return ClaimLoot(C,Nearest->Spec.Id);
    if(Service=="SupplyA"||Service=="SupplyB")
    {
        if(!Next.Available(0)||Next.Evidence.Contains(Service))return TEXT("Already collected for this character.");
        if(!Next.Add("Supply",1))return TEXT("Inventory full.");Next.Observe(Service);Next.Claim(0);
    }
    else if(Service=="Teacher")
    {
        if(!Next.Claims.Contains(FAetherProfile::QuestId(1)))return TEXT("Register and bind the inn first.");
        Next.LearnedSpells|=3;
        if(Next.Claims.Contains(FAetherProfile::QuestId(3)))Next.LearnedSpells|=4;
        if(Next.Claims.Contains(FAetherProfile::QuestId(4)))Next.LearnedSpells|=8;
        if(!Commit(PS,Next))return TEXT("Storage unavailable; try again.");
        if(Next.Available(2))
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
        Next.Observe("Inn");Next.Claim(1);
        if(!Commit(PS,Next))return TEXT("Storage unavailable.");
        if(C->TimeSinceDamage()<8)return TEXT("Rest requires eight seconds out of combat.");
        C->SetVitals(C->MaxHealth,100,100);C->WaterReserveKg=3;return TEXT("Checkpoint bound. Weapons issued once; R equips. Rested.");
    }
    else if(Service=="Shop")
    {const auto& R=FAetherRules::Get().Items.FindChecked("Potion");if(Next.Gold<R.Buy||!Next.Add("Potion",1))return TEXT("Need 20 gold and inventory space. I: 7 mana / 8 ration / Delete sell.");Next.Gold-=R.Buy;}
    else if(Service=="Daily"||Service=="DailyPatrol"||Service=="DailyFire")
    {
        if(!Next.Claims.Contains(FAetherProfile::QuestId(7)))return TEXT("Finish the main story to unlock commissions.");
        const int32 Template=Service=="Daily"?0:Service=="DailyPatrol"?1:2;
        if(!Next.ClaimDaily(Template))
        {
            if(Template==2&&!Next.DailyClaims.Contains("Daily2"))
            {
                for(int32 I=0;I<3;++I)
                {
                    FName Id=*FString::Printf(TEXT("Commission_%s_%s_%d"),*Next.CharacterId,*Next.DailyDate,I);
                    if(!Prop(Id)){auto* Fire=Make(Id,*FString::Printf(TEXT("DailyFire%d"),I),{-27000.f+I*250,1400,50},{.8,.8,1},EAetherObjectKind::Timber,TEXT("COMMISSION / EXTINGUISH"));Fire->SetOwner(C);FReactiveStimulus H;H.HeatJ=60000;Fire->Reactive->Inject(H);}
                }
            }
            return Template==0?TEXT("Bring two supplies; one reward per UTC day."):Template==1?TEXT("Visit the three patrol markers, then return."):TEXT("Extinguish your three commission fires in Ashwood, then return.");
        }
    }
    else if(Service.ToString().StartsWith("Patrol"))
    {if(!Next.Claims.Contains(FAetherProfile::QuestId(7)))return TEXT("Patrol unlocks after the story.");Next.DailyEvidence.AddUnique(Service);}
    else if(Service=="Gather")
    {
        if(Next.DailyEvidence.Contains(Nearest->Spec.Id))return TEXT("This supply cache was collected today.");
        if(!Next.Add("Supply",1)||!Next.Add("Herb",2))return TEXT("Inventory full.");Next.DailyEvidence.Add(Nearest->Spec.Id);
    }
    else if(Service=="Recruit")return RecruitCompanion(C,false);
    else if(Service=="Well")
    {
        const double Water=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->WithdrawWater(Nearest->Reactive,FMath::Max(0.f,3-C->WaterReserveKg));C->WaterReserveKg+=Water;
        return FString::Printf(TEXT("Transferred %.2f kg. This reservoir is finite."),Water);
    }
    else if(Service=="Bucket")
    {
        AAetherFrontierProp* Fire=nullptr;double Distance=FMath::Square(600.0);
        for(AAetherFrontierProp* P:Props)if(P->Spec.Id.ToString().StartsWith("ForestFire")){double D=FVector::DistSquared(P->GetActorLocation(),Nearest->GetActorLocation());if(D<Distance){Distance=D;Fire=P;}}
        if(!Fire)return TEXT("No fire beside this bucket.");
        const double Water=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->WithdrawWater(Nearest->Reactive,.5);
        if(Water>0){FReactiveStimulus Splash;Splash.SourceActor=C;Splash.WaterKg=Water;Fire->Reactive->Inject(Splash);}
        else if(AetherGuide::CanInspectFire(Fire)){Observe(C,Fire->Service);return TEXT("水桶已空，已检查清理后的火点。");}
        return Water>0?TEXT("已把桶中现有的水泼向火点。"):TEXT("水桶已空；火点仍不安全，需补水或使用引泉。");
    }
    else if(Service=="HingedGate"){Nearest->Mechanism->bGateOpen=!Nearest->Mechanism->bGateOpen;return TEXT("Gate motor toggled; physical obstructions resist its limited force.");}
    else if(Service=="Source"){State->bPowerOn=!State->bPowerOn;SaveWorld();return State->bPowerOn?TEXT("Power on."):TEXT("Power off; no residual charge in the rod.");}
    else if(Service=="SupplyRestored"||Service=="Receiver")
    {
        if(Service=="Receiver"&&!State->bSupplyRestored&&Nearest->ReceivedPower<1)return TEXT("No power: place the metal rod touching both contacts, or use the mechanical pump.");
        State->bSupplyRestored=true;Next.Observe("SupplyRestored");Next.Claim(4);
    }
    else if(Service=="Abbey")
    {
        if(!Next.Available(6)&&!Next.Claims.Contains(FAetherProfile::QuestId(6)))return TEXT("Complete both field quests and recruit first.");
        return Encounters?Encounters->Start(C,false):TEXT("Encounter unavailable.");
    }
    else if(Service=="AbbeyValve")return Encounters?Encounters->Channel(C):TEXT("Encounter unavailable.");
    else if(Service=="GuardianDefeated")
    {
        if(!IsValid(Guardian)||Guardian->Alive()||!KillCredit.FindRef(Guardian).Contains(*Next.CharacterId))return TEXT("No completed encounter participation.");
        Next.Observe(Service);Next.Claim(6);
    }
    else if(Service=="Activity")
    {
        if(!Next.Claims.Contains(FAetherProfile::QuestId(7)))return TEXT("Public defence unlocks after the main story.");
        if(!Encounters)return TEXT("Encounter unavailable.");
        if(Encounters->Relay.Phase==EAetherEncounterPhase::Channel)return Encounters->Channel(C);
        return Encounters->Start(C,true);
    }
    else if(Service=="SealDelivered")
    {
        if(!Next.Available(7)||Next.Count("AncientSeal")<1)return TEXT("Bring the ancient seal from your completed encounter.");
        Next.Observe(Service);Next.Claim(7);
    }
    else {if(!Next.Observe(Service))return TEXT("No new objective here; J shows current requirements.");for(int32 Q=0;Q<8;++Q)Next.Claim(Q);}
    if(!Commit(PS,Next))return TEXT("Storage unavailable; no inventory or reward change committed. Retry.");
    return TEXT("Interaction committed. J: quests / I: inventory / K: abilities.");
}
