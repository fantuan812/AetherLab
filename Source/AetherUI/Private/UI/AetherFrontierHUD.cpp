#include "UI/AetherFrontierHUD.h"
#include "AetherFrontier.h"
#include "AetherFrontierPanel.h"
#include "Tests/AetherMenuInteractionProbe.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/PlatformMisc.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"

void AAetherFrontierHUD::BeginPlay()
{Super::BeginPlay();if(PlayerOwner&&PlayerOwner->IsLocalController()){PanelWidget=CreateWidget<UAetherFrontierPanel>(PlayerOwner,UAetherFrontierPanel::StaticClass());if(PanelWidget)PanelWidget->AddToViewport();}}
void AAetherFrontierHUD::DrawHUD()
{
    Super::DrawHUD();AetherMenuInteraction::Tick(this,PanelWidget);auto* C=Cast<AAetherFrontierCharacter>(GetOwningPawn());auto* PS=C?C->ProfileState():nullptr;if(!Canvas||!C||!PS)return;
    const float W=Canvas->SizeX,H=Canvas->SizeY;const auto& P=PS->Profile;
    DrawRect(FLinearColor(.018,.026,.04,.92),20,20,400,108);
    DrawText(TEXT("AETHERLAB / EMBER FRONTIER"),FLinearColor(.9,.74,.4),34,29,nullptr,1.2);
    const float Values[]={C->Health(),C->Mana(),C->Stamina()};const FLinearColor Colors[]={FLinearColor(.75,.2,.18),FLinearColor(.18,.45,.85),FLinearColor(.24,.7,.45)};
    for(int32 I=0;I<3;++I){DrawRect(FLinearColor(.1,.13,.17),34,58+I*20,260,10);DrawRect(Colors[I],34,58+I*20,260*FMath::Clamp(Values[I]/100,0.f,1.f),10);DrawText(FString::Printf(TEXT("%.0f"),Values[I]),FLinearColor::White,310,54+I*20);}
    DrawRect(FLinearColor(.018,.026,.04,.92),20,H-105,W-40,85);
    const TCHAR* Names[]={TEXT("HEAT"),TEXT("WATER"),TEXT("FROST"),TEXT("LIGHTNING")};
    DrawText(FString::Printf(TEXT("[%d %s] %s   Water %.1f kg   Gold %d   Level %d"),C->SelectedSpell+1,Names[C->SelectedSpell],C->SpellUnlocked(C->SelectedSpell)?TEXT("READY"):TEXT("LOCKED"),C->WaterReserveKg,P.Gold,FMath::Clamp(1+P.Experience/200,1,5)),FLinearColor(.9,.74,.4),34,H-94);
    DrawText(TEXT("WASD Move | Shift Sprint | Space Jump | Ctrl Crouch | Alt Dodge | LMB/Hold Attack | RMB Guard | 1-4/MMB Magic | F Lock | E Interact"),FLinearColor::White,34,H-70);
    DrawText(TEXT("I Bag  J Quests  K Skills  M Map  P Party  Q Potion  G Carry  V Push  R/T Equip  F5 Save  Esc Menu"),FLinearColor(.65,.73,.8),34,H-48);
    DrawText(C->Feedback,FLinearColor(1,.84,.48),30,140);
    if(GetWorld()->GetTimeSeconds()>=NextGuidanceUpdate)
    {NextGuidanceUpdate=GetWorld()->GetTimeSeconds()+.15f;Guidance=AetherGuide::Resolve(C);C->RefreshInteractionFocus();Interaction=C->InteractionFocus;}
    const float GX=FMath::Max(430.f,W-370.f);float Y=38;
    DrawText(Guidance.Title,FLinearColor(.96f,.82f,.5f),GX,Y);Y+=23;
    DrawText(Guidance.Label,FLinearColor::White,GX,Y);Y+=21;
    // Canvas has no automatic wrapping: split the short authored hint into bounded rows.
    for(int32 I=0;I<Guidance.Hint.Len();I+=22){DrawText(Guidance.Hint.Mid(I,22),FLinearColor(.7f,.8f,.9f),GX,Y);Y+=19;}
    if(Guidance.bHasTarget)
    {
        const FVector Delta=Guidance.Position-C->GetActorLocation();const float Meters=Delta.Size2D()/100.f;
        DrawText(FString::Printf(TEXT("目标距离 %.0f 米 / J 切换追踪"),Meters),FLinearColor(.96f,.82f,.5f),GX,Y);
        if(!C->bPanel)
        {
            const FVector Projected=Project(Guidance.Position+FVector(0,0,100));const FVector Facing=PlayerOwner->GetControlRotation().Vector();
            if(FVector::DotProduct(Delta,Facing)>0&&Projected.X>30&&Projected.X<W-30&&Projected.Y>180&&Projected.Y<H-160)
            {DrawRect(FLinearColor(1.f,.75f,.2f),Projected.X-4,Projected.Y-4,8,8);DrawText(FString::Printf(TEXT("%.0f m"),Meters),FLinearColor::White,Projected.X+10,Projected.Y-8);}
            else
            {
                const float Angle=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(PlayerOwner->GetControlRotation().Yaw,Delta.Rotation().Yaw));
                const FVector2D Center(W*.5f,175),Direction(FMath::Sin(Angle),-FMath::Cos(Angle)),Side(-Direction.Y,Direction.X),Tip=Center+Direction*17;
                DrawLine(Center.X,Center.Y,Tip.X,Tip.Y,FLinearColor(1.f,.75f,.2f),2);
                for(float Sign:{-1.f,1.f}){const FVector2D Tail=Tip-Direction*7+Side*Sign*5;DrawLine(Tip.X,Tip.Y,Tail.X,Tail.Y,FLinearColor(1.f,.75f,.2f),2);}
            }
        }
    }
    if(!C->bPanel&&!Interaction.Prompt.IsEmpty())DrawText(Interaction.Prompt,FLinearColor(1.f,.87f,.5f),FMath::Max(30.f,W*.5f-220),H*.62f);
    DrawLine(W/2-7,H/2,W/2+7,H/2,FLinearColor::White);DrawLine(W/2,H/2-7,W/2,H/2+7,FLinearColor::White);
    if(!C->Alive())DrawText(TEXT("DOWNED / Ally E: revive 3s / F8: recover at checkpoint"),FLinearColor(1,.3,.2),W*.35,H*.45,nullptr,1.6);
    float EY=190;for(TActorIterator<AAetherEncounterDirector> It(GetWorld());It;++It)
    {
        for(const auto* Run:{&It->Abbey,&It->Relay})if(Run->Phase!=EAetherEncounterPhase::Idle)
        {DrawText(FString::Printf(TEXT("%s phase %d / wave %d / channel %.1fs / %d seats"),*Run->Definition.ToString(),int32(Run->Phase),Run->Wave+1,Run->Progress,Run->LockedSeats),FLinearColor(.8,.85,1),30,EY);EY+=20;}
    }
    for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(It->Fighter==EAetherFighter::BellKnight&&It->Alive()&&FVector::DistSquared(It->GetActorLocation(),C->GetActorLocation())<FMath::Square(2500.))
    {const TCHAR* Phases[]={TEXT("ARMORED / BREAK POSTURE"),TEXT("OVERHEATING / WATER TO EXPOSE"),TEXT("EXPOSED / 8 SECOND WINDOW")};DrawText(Phases[It->BossPhase%3],FLinearColor(1,.65,.15),W*.35,165);}
    if(C->bDebugOverlay)
    {
        auto* World=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();float YDebug=280;
        DrawText(World->GetStatsText(),FLinearColor(.6,1,.7),30,YDebug);YDebug+=22;
        if(const auto* Sim=World->GetSimulation()){const auto& A=Sim->GetStats();DrawText(FString::Printf(TEXT("Electric J: input %.1f = heat %.1f + device %.1f + loss %.1f | duplicate %llu"),A.ElectricalInputJ,A.ElectricalDepositedJ,A.ElectricalUsefulJ,A.ElectricalLostJ,A.DuplicateInputs),FLinearColor::White,30,YDebug);YDebug+=22;
        DrawText(FString::Printf(TEXT("Liquid moved %.3f kg / %.1f J | exported %.3f kg / %.1f J | cut %.1f = used %.1f + unused %.1f J"),A.TransferredWaterKg,A.TransferredEnthalpyJ,A.WithdrawnWaterKg,A.WithdrawnEnthalpyJ,A.CuttingDeliveredJ,A.CuttingAbsorbedJ,A.CuttingUnusedJ),FLinearColor::White,30,YDebug);YDebug+=22;}
        FHitResult Hit;FVector Origin,Direction;if(C->FindSpellTarget(1,Hit,Origin,Direction))if(auto* Body=Hit.GetActor()?Hit.GetActor()->FindComponentByClass<UReactiveBodyComponent>():nullptr)
        {const auto& V=Body->State;DrawText(FString::Printf(TEXT("%s #%u / T %.1f C / water %.3f kg / electrical water %.3f kg / wet %.2f / fuel %.3f kg / integrity %.2f / chain %llu"),*Body->StableId.ToString(),Body->GetBodyId(),V.TemperatureC,V.WaterKg,V.ElectricalWaterKg,V.ElectricalWetness01,V.FuelKg,V.Integrity,Body->LastReactionChain),FLinearColor::White,30,YDebug);YDebug+=22;
        if(auto* M=Hit.GetActor()->FindComponentByClass<UReactiveMechanismComponent>())DrawText(FString::Printf(TEXT("Source %.1f W / energy %.1f J / on %d / support released %d / gate %d"),M->PowerW,M->RemainingEnergyJ,M->bPowerEnabled,M->bReleased,M->bGateOpen),FLinearColor::White,30,YDebug);}
    }
    if(!C->bPanel||(PanelWidget&&C->Panel!=4))return;
    DrawRect(FLinearColor(.015,.024,.038,.97),W*.17,H*.16,W*.66,H*.65);Y=H*.19;const float X=W*.2;
    auto Line=[&](const FString& S){DrawText(S,FLinearColor(.87,.91,.95),X,Y,nullptr,1.1);Y+=25;};
    if(C->Panel==1){Line(TEXT("INVENTORY / Tab select / B split / N merge / Del sell / 7-8 shop"));if(!P.Inventory.IsEmpty())C->SelectedItem%=P.Inventory.Num();int32 Row=0;for(const auto& I:P.Inventory)Line(FString(Row++==C->SelectedItem?TEXT("> "):TEXT("  "))+FString::Printf(TEXT("%s x%d %s"),*I.DefinitionId.ToString(),I.Count,P.Equipped.FindKey(I.InstanceId)?TEXT("[EQUIPPED]"):TEXT("")));}
    if(C->Panel==2){Line(TEXT("PERSONAL JOURNAL"));for(const auto& Rule:FAetherRules::Get().Quests){const FName Q=Rule.Id;Line(FString(P.Claims.Contains(Q)?TEXT("[DONE] "):P.Available(Q)?TEXT("[ACTIVE] "):TEXT("[LOCKED] "))+FAetherProfile::QuestTitle(Q));}}
    if(C->Panel==3){Line(TEXT("ABILITIES / learned permanently from the town teacher"));for(int32 I=0;I<4;++I)Line(FString::Printf(TEXT("%d %s  %s  / mana %.0f"),I+1,Names[I],C->SpellUnlocked(I)?TEXT("LEARNED"):TEXT("LOCKED"),UAetherSpellAbility::Cost(I)));}
    if(C->Panel==4)
    {
        Line(TEXT("WORLD MAP / north is up / 800m graybox"));
        const FVector Places[]={{0,0,0},{-6500,-29000,0},{-27000,0,0},{27000,0,0},{0,27000,0},{25000,22000,0}};
        const TCHAR* Titles[]={TEXT("Town"),TEXT("Start"),TEXT("Ashwood"),TEXT("Waterworks"),TEXT("Abbey"),TEXT("Relay")};
        const float MX=W*.5,MY=H*.5,K=H*.000006;
        for(int32 I=0;I<6;++I){const float PX=MX+Places[I].X*K,PY=MY-Places[I].Y*K;DrawRect(FLinearColor(.9,.7,.35),PX-4,PY-4,8,8);DrawText(Titles[I],FLinearColor::White,PX+8,PY-8);}
        const FVector L=C->GetActorLocation();DrawRect(FLinearColor(.2,1,.6),MX+L.X*K-4,MY-L.Y*K-4,8,8);
        if(Guidance.bHasTarget){const float TX=MX+Guidance.Position.X*K,TY=MY-Guidance.Position.Y*K;DrawRect(FLinearColor(1.f,.4f,.15f),TX-5,TY-5,10,10);DrawText(Guidance.Label,FLinearColor(1.f,.8f,.4f),TX+10,TY+8);}

    }
    if(C->Panel==5){Line(TEXT("PARTY / Y invite / U accept / O leave / H command / Del dismiss"));for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(It->Fighter==EAetherFighter::Player)Line(FString::Printf(TEXT("%s  HP %.0f %s"),It->ProfileState()?*It->ProfileState()->DisplayName:It->bHealer?TEXT("Healer companion"):TEXT("Guard companion"),It->Health(),It->ReviveTarget?TEXT("REVIVING"):TEXT("")));}
    if(C->Panel==6){Line(TEXT("MENU / Esc closes / server keeps running"));Line(TEXT("F5 saves world. Profile transactions save automatically."));Line(TEXT("Restart with the same DevProfile to reconnect to local saved progress."));Line(TEXT("Prototype identity only; no production account authentication."));}
}

void AAetherFrontierHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    // 本地连接切图或断线时，主动释放视口 Widget，避免留下旧角色引用。
    if (PanelWidget) PanelWidget->RemoveFromParent();
    PanelWidget = nullptr;
    Super::EndPlay(Reason);
}
