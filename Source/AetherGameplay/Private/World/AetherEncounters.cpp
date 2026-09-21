#include "World/AetherEncounters.h"
#include "Framework/AetherFrontier.h"
#include "Framework/AetherRules.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
AAetherEncounterDirector::AAetherEncounterDirector()
{bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=.1;Abbey.Definition="Abbey";Relay.Definition="Relay";}
void AAetherEncounterDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AAetherEncounterDirector,Abbey);DOREPLIFETIME(AAetherEncounterDirector,Relay);}
bool AAetherEncounterDirector::Participates(const AAetherFrontierCharacter* C,FName Encounter) const
{
    if(!C)return false;if(C->CompanionOwner)C=C->CompanionOwner;
    const auto* PS=C->ProfileState();if(!PS)return false;
    const auto& R=Encounter=="Abbey"?Abbey:Relay;return R.Participants.Contains(PS->Profile.CharacterId);
}
void AAetherEncounterDirector::SetPhase(FAetherEncounterRun& R,EAetherEncounterPhase P)
{R.Phase=P;++R.Version;R.PhaseStarted=GetWorld()->GetTimeSeconds();ForceNetUpdate();}
FString AAetherEncounterDirector::Start(AAetherFrontierCharacter* C,bool Public)
{
    if(!HasAuthority()||!C||!C->ProfileState())return TEXT("No authority/profile.");
    auto& R=Public?Relay:Abbey;
    const float Now=GetWorld()->GetTimeSeconds();
    if(R.Phase!=EAetherEncounterPhase::Idle&&R.Phase!=EAetherEncounterPhase::Failed&&R.Phase!=EAetherEncounterPhase::Succeeded)
    {
        if(!Public&&!Participates(C,"Abbey"))return TEXT("Another party owns this challenge.");
        if(Public)R.Participants.AddUnique(C->ProfileState()->Profile.CharacterId);
        return TEXT("Encounter already running; current phase is shown on the HUD.");
    }
    Settle(R);
    if(R.Phase==EAetherEncounterPhase::Succeeded&&R.Settled.Num()<R.Participants.Num())return TEXT("Rewards await participant reconnection/storage; cannot overwrite this instance.");
    if(R.Phase==EAetherEncounterPhase::Failed&&Now-R.PhaseStarted<15)return TEXT("Encounter resetting; return in 15 seconds.");
    FAetherEncounterRun Next;Next.Definition=R.Definition;Next.Instance=FGuid::NewGuid();Next.Participants.Add(C->ProfileState()->Profile.CharacterId);
    int32 Seats=1;
    for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)
    {
        if(*It==C||It->Fighter!=EAetherFighter::Player||!It->Alive()||FVector::DistSquared(C->GetActorLocation(),It->GetActorLocation())>FMath::Square(2500.))continue;
        if(It->CompanionOwner&&(Public||It->CompanionOwner==C||(It->CompanionOwner->ProfileState()&&It->CompanionOwner->ProfileState()->PartyLeader==C->ProfileState()->PartyLeader)))++Seats;
        else if(It->ProfileState()&&(Public||(It->ProfileState()->PartyLeader==C->ProfileState()->PartyLeader&&(It->ProfileState()->Profile.Available("Q_Main_07")||It->ProfileState()->Profile.Claims.Contains(FName("Q_Main_07"))))))
        {Next.Participants.AddUnique(It->ProfileState()->Profile.CharacterId);++Seats;}
    }
    if(!Public&&Seats<2)return TEXT("At least two nearby humans/companions are required.");
    Next.LockedSeats=FMath::Clamp(Seats,2,4);R=Next;SetPhase(R,EAetherEncounterPhase::Front);
    SpawnWave(R,Public?RelayEnemies:AbbeyEnemies);
    return Public?TEXT("Relay defence: clear three finite waves, then stabilize the relay."):TEXT("Abbey challenge: courtyard, three valve waves, elite, then bell guardian.");
}
void AAetherEncounterDirector::SpawnWave(FAetherEncounterRun& R,TArray<TObjectPtr<AAetherFrontierCharacter>>& Enemies)
{
    for(const auto& E:Enemies)if(IsValid(E))E->Destroy();Enemies.Reset();
    auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();if(!M)return;
    const bool Public=R.Definition=="Relay",Boss=R.Phase==EAetherEncounterPhase::Boss;
    const FName Definition=Public?FName(*FString::Printf(TEXT("Relay%d"),R.Wave)):R.Phase==EAetherEncounterPhase::Front?FName("AbbeyFront"):R.Phase==EAetherEncounterPhase::Channel?FName("AbbeyChannel"):Boss?FName("AbbeyBoss"):FName("AbbeyElite");
    const auto* Rule=FAetherRules::Get().Encounters.Find(Definition);if(!Rule){SetPhase(R,EAetherEncounterPhase::Failed);return;}
    const FVector Center=Rule->Center;const int32 Count=Rule->Types.Num();
    for(int32 I=0;I<Count;++I)
    {
        const auto Type=static_cast<EAetherFighter>(Rule->Types[I]);
        auto* E=M->SpawnFighter(Center+FVector((I-Count/2)*240,120*(I%2),0),Type,NAME_None);
        E->EncounterId=R.Definition;E->MaxHealth*=R.LockedSeats==2?1:R.LockedSeats==3?1.3:1.65;
        if(R.Phase==EAetherEncounterPhase::Elite)E->MaxHealth*=2;
        E->SetVitals(E->MaxHealth,100,100);Enemies.Add(E);
        for(const auto& Id:R.Participants)M->KillCredit.FindOrAdd(E).Add(*Id);
        if(Boss){M->Guardian=E;E->BossPhase=0;E->BossPhaseStarted=GetWorld()->GetTimeSeconds();}
    }
}
FString AAetherEncounterDirector::Channel(AAetherFrontierCharacter* C,bool UseCompanion)
{
    if(!C||!C->Alive())return TEXT("Cannot channel while downed.");
    auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();if(!M)return TEXT("No encounter.");
    const bool Public=FVector::DistSquared(C->GetActorLocation(),FVector(25000,22000,100))<FMath::Square(300.);
    auto& R=Public?Relay:Abbey;
    auto& Channeler=Public?RelayChanneler:AbbeyChanneler;auto& ChannelDamageSerial=Public?RelayChannelDamage:AbbeyChannelDamage;
    if(!IsValid(Channeler))Channeler=nullptr;
    if(!Participates(C,R.Definition)||R.Phase!=EAetherEncounterPhase::Channel)return TEXT("No active channel stage for your party.");
    auto* Valve=M->Prop(Public?"Activity":"AbbeyValve");if(!Valve||FVector::DistSquared(C->GetActorLocation(),Valve->GetActorLocation())>FMath::Square(300.))return TEXT("Stand beside the valve to begin.");
    if(Channeler&&Channeler!=C)return TEXT("Channel already occupied.");
    if(UseCompanion)
    {
        for(const auto& B:M->Companions)if(IsValid(B)&&B->CompanionOwner==C&&B->Alive()){Channeler=B;B->bCompanionHold=false;break;}
        if(!Channeler)return TEXT("No available companion.");
    }
    else Channeler=C;
    Channeler->EncounterId=R.Definition;ChannelDamageSerial=Channeler->CombatRuntime->DamageReceivedCount;
    return TEXT("Channeling: remain beside valve and avoid damage. Three seconds per wave.");
}
void AAetherEncounterDirector::Settle(FAetherEncounterRun& R)
{
    if(R.Phase!=EAetherEncounterPhase::Succeeded)return;
    auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();if(!M)return;
    if(M->IsNativeMode()){M->SettleNativeEncounter(R);return;}
    for(const auto& Id:R.Participants)
    {
        if(R.Settled.Contains(Id))continue;
        AAetherPlayerState* Online=nullptr;for(TActorIterator<AAetherPlayerState> It(GetWorld());It;++It)if(It->Profile.CharacterId==Id){Online=*It;break;}
        const auto* Stored=M->Database->Profiles.FindByPredicate([&](const auto& P){return P.CharacterId==Id;});
        if(!Online&&!Stored)continue;auto Next=Online?Online->Profile:*Stored;
        FGuid& Receipt=R.Definition=="Relay"?Next.LastRelayReceipt:Next.LastAbbeyReceipt;
        if(Receipt==R.Instance){R.Settled.AddUnique(Id);continue;}
        Receipt=R.Instance;
        Next.RefreshDaily(FDateTime::UtcNow().ToString(TEXT("%Y%m%d")));
        const FName Key=R.Definition=="Relay"?FName("RelayReward"):FName("AbbeyReward");
        if(R.Definition=="Abbey"){Next.Observe("GuardianDefeated");Next.TryAutoClaim("Q_Main_07");}
        if(!Next.DailyClaims.Contains(Key))
        {if(Next.PendingGold>999940||Next.PendingMaterial>997)continue;Next.PendingGold+=60;Next.PendingMaterial+=3;Next.DailyClaims.Add(Key);}
        Next.CollectPending();
        if(Online?M->Commit(Online,Next):M->CommitOffline(Next))R.Settled.Add(Id);
    }
}
void AAetherEncounterDirector::UpdateRun(FAetherEncounterRun& R,TArray<TObjectPtr<AAetherFrontierCharacter>>& Enemies,float Dt,float& EmptySince)
{
    if(R.Phase==EAetherEncounterPhase::Succeeded){Settle(R);return;}
    if(R.Phase==EAetherEncounterPhase::Idle||R.Phase==EAetherEncounterPhase::Failed)return;
    const float Now=GetWorld()->GetTimeSeconds();const bool Public=R.Definition=="Relay";
    auto& Channeler=Public?RelayChanneler:AbbeyChanneler;auto& ChannelDamageSerial=Public?RelayChannelDamage:AbbeyChannelDamage;
    if(!IsValid(Channeler))Channeler=nullptr;
    const FVector Center=Public?FVector(25000,22000,0):FVector(0,26500,0);
    bool Any=false;for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)
        if(It->Fighter==EAetherFighter::Player&&It->Alive()&&Participates(*It,R.Definition)&&FVector::DistSquared(It->GetActorLocation(),Center)<FMath::Square(5500.)){Any=true;break;}
    if(Any)EmptySince=0;else if(EmptySince==0)EmptySince=Now;
    if((EmptySince>0&&Now-EmptySince>10)||Now-R.PhaseStarted>600)
    {SetPhase(R,EAetherEncounterPhase::Failed);for(const auto& E:Enemies)if(IsValid(E))E->Destroy();Enemies.Reset();Channeler=nullptr;return;}
    int32 Living=0;for(const auto& E:Enemies)if(IsValid(E)&&E->Alive())++Living;
    if(R.Phase==EAetherEncounterPhase::Boss)
    {
        if(Living==0){SetPhase(R,EAetherEncounterPhase::Succeeded);Settle(R);return;}
        auto* Boss=Enemies[0].Get();const float Age=Now-Boss->BossPhaseStarted;
        if((Boss->BossPhase==0&&(Age>12||Boss->BossPressure>=40))||(Boss->BossPhase==1&&(Age>6||Boss->Reactive->State.ElectricalWetness01>.25))||(Boss->BossPhase==2&&Age>8))
        {Boss->BossPhase=(Boss->BossPhase+1)%3;Boss->BossPhaseStarted=Now;++Boss->BossVersion;Boss->BossPressure=0;Boss->ForceNetUpdate();}
        if(Boss->BossPhase==2){Boss->Equipment->CancelAttack();Boss->StunUntil=Now+.2f;}
        else if(Boss->BossPhase==1)
        {
            for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(It->Fighter==EAetherFighter::Player&&It->Alive()&&Participates(*It,R.Definition)&&FVector::DistSquared(It->GetActorLocation(),Boss->GetActorLocation())<FMath::Square(450.))
            {FReactiveStimulus S;S.SourceActor=Boss;S.HeatJ=500*Dt;It->Reactive->Inject(S);}
        }
        return;
    }
    if(R.Phase==EAetherEncounterPhase::Channel)
    {
        auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();auto* Valve=M?M->Prop(Public?"Activity":"AbbeyValve"):nullptr;
        if(Channeler&&Channeler->EncounterId==R.Definition&&Valve)
        {
            const FVector D=Valve->GetActorLocation()-Channeler->GetActorLocation();
            if(!Channeler->Alive()||Channeler->CombatRuntime->DamageReceivedCount!=ChannelDamageSerial||Channeler->CombatTime()<Channeler->StunUntil){Channeler=nullptr;}
            else if(D.Size()>250){if(Channeler->CompanionOwner)Channeler->AddMovementInput(Channeler->SafeMoveDirection(Valve->GetActorLocation()));else Channeler=nullptr;}
            else
            {
                FCollisionQueryParams Q(SCENE_QUERY_STAT(Channel),false,Channeler);Q.AddIgnoredActor(Valve);
                if(!GetWorld()->LineTraceTestByChannel(Channeler->GetActorLocation(),Valve->GetActorLocation(),ECC_Visibility,Q))R.Progress+=FMath::Min(Dt,.2f);
            }
        }
        if(R.Progress>=3*(R.Wave+1)&&Living==0)
        {
            if(Public){SetPhase(R,EAetherEncounterPhase::Succeeded);Channeler=nullptr;Settle(R);}
            else if(++R.Wave>=3){SetPhase(R,EAetherEncounterPhase::Elite);Channeler=nullptr;SpawnWave(R,Enemies);}
            else SpawnWave(R,Enemies);
        }
        return;
    }
    if(Living>0)return;
    if(Public)
    {
        if(++R.Wave<3)SpawnWave(R,Enemies);else{R.Wave=0;SetPhase(R,EAetherEncounterPhase::Channel);}
    }
    else if(R.Phase==EAetherEncounterPhase::Front){R.Wave=0;SetPhase(R,EAetherEncounterPhase::Channel);SpawnWave(R,Enemies);}
    else if(R.Phase==EAetherEncounterPhase::Elite){SetPhase(R,EAetherEncounterPhase::Boss);SpawnWave(R,Enemies);}
}
void AAetherEncounterDirector::Tick(float Dt)
{Super::Tick(Dt);if(!HasAuthority())return;CampTimer+=Dt;if(CampTimer>=1){CampTimer=0;UpdateCamps();}UpdateRun(Abbey,AbbeyEnemies,Dt,EmptySinceAbbey);UpdateRun(Relay,RelayEnemies,Dt,EmptySinceRelay);}

void AAetherEncounterDirector::UpdateCamps()
{
    auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();if(!M||M->bSmoke)return;
    if(Camps.IsEmpty())for(const auto& P:FAetherRules::Get().Encounters)if(P.Value.RespawnSeconds>0){FAetherCamp Camp;Camp.Definition=P.Key;
     if(const auto* Saved=M->Database->CampReceipts.FindByPredicate([&](const auto& R){return R.Definition==P.Key;}))
     {Camp.bSpawned=true;Camp.bRewardCreated=true;Camp.Instance=Saved->Instance;const double Remaining=FMath::Clamp(double(Saved->RespawnAfterUtc-FDateTime::UtcNow().ToUnixTimestamp()),0.,double(P.Value.RespawnSeconds));Camp.ClearedAt=GetWorld()->GetTimeSeconds()-P.Value.RespawnSeconds+Remaining;}
     Camps.Add(Camp);}
    const float Now=GetWorld()->GetTimeSeconds();
    for(auto& Camp:Camps)
    {
        const auto* Rule=FAetherRules::Get().Encounters.Find(Camp.Definition);if(!Rule)continue;
        double Distance=1.e30;for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(It->ProfileState())Distance=FMath::Min(Distance,FVector::Distance(Rule->Center,It->GetActorLocation()));
        if(!Camp.bSpawned&&Distance<2200)
        {
            Camp.bSpawned=true;Camp.Instance=FGuid::NewGuid();Camp.ClearedAt=0;Camp.bRewardCreated=false;
            for(int I=0;I<Rule->Types.Num();++I)Camp.Enemies.Add(M->SpawnFighter(Rule->Center+FVector(I*180,0,0),static_cast<EAetherFighter>(Rule->Types[I]),NAME_None));
        }
        if(!Camp.bSpawned)continue;
        bool Alive=false;for(const auto& Enemy:Camp.Enemies)Alive|=IsValid(Enemy)&&Enemy->Alive();
        if(!Alive&&!Camp.bRewardCreated)
        {if(!M->RecordCampClear(Camp.Definition,Camp.Instance))continue;Camp.bRewardCreated=true;Camp.ClearedAt=Now;}

        if(!Alive&&Camp.bRewardCreated&&Now-Camp.ClearedAt>=Rule->RespawnSeconds&&Distance>3000)
        {for(const auto& Enemy:Camp.Enemies)if(IsValid(Enemy))Enemy->Destroy();Camp.Enemies.Reset();Camp.bSpawned=false;}
    }
}
