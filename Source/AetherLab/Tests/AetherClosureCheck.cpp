#include "../AetherFrontier.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Skills/AetherSkillDefinitions.h"
#include "Presentation/AetherPresentation.h"
#include "Modules/ModuleManager.h"
#include "GameFramework/HUD.h"
#include "../AetherTraversal.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"

namespace {
bool ClosureServer(){return FParse::Param(FCommandLine::Get(),TEXT("AetherV807Server"));}
bool ClosureClient(){return FParse::Param(FCommandLine::Get(),TEXT("AetherV807Client"));}
const FGuid LootReceipt(0xA375E807,0,0,20);
const FName LootName=*FString("Loot_"+LootReceipt.ToString(EGuidFormats::Digits));
}
void AAetherFrontierCharacter::ClientClosureAction_Implementation(FName Action,FRotator Look)
{
#if !UE_BUILD_SHIPPING
    if(!ClosureClient()||!IsLocallyControlled())return;
    if(Action=="Disconnect"){UE_LOG(LogTemp,Display,TEXT("V807_VOLUNTARY_DISCONNECT"));FPlatformMisc::RequestExit(false);return;}
    if(Controller)Controller->SetControlRotation(Look);
    UE_LOG(LogTemp,Display,TEXT("V807_CLIENT_ACTION id=%s action=%s"),ProfileState()?*ProfileState()->Profile.CharacterId:TEXT("pending"),*Action.ToString());
    if(Action=="InventoryProbe")
    {
        const auto* PS=ProfileState();if(!PS)return;
        const auto* Stack=PS->Profile.Inventory.FindByPredicate([](const auto& I){return I.DefinitionId=="Potion"&&I.Count>1;});if(!Stack)return;
        PendingInventory=FAetherInventoryCommand();PendingInventory.CommandId=FGuid(0xA379,0,0,PS->Profile.CharacterId=="Alpha"?11:12);
        PendingInventory.Action="Split";PendingInventory.Quantity=1;PendingInventory.ExpectedInventoryRevision=PS->Profile.Revision;PendingInventory.ItemInstanceId=Stack->InstanceId;
        ServerInventory(PendingInventory);ServerInventory(PendingInventory);return;
    }
    if(Action=="InventoryStale")
    {
        const auto* PS=ProfileState();if(!PS||PS->Profile.InventoryReceipts.IsEmpty())return;
        auto Command=PS->Profile.InventoryReceipts.Last().Command;Command.CommandId=FGuid::NewGuid();ServerInventory(Command);return;
    }
    ServerAction(Action);
#endif
}
void AAetherFrontierCharacter::ServerClosureAck_Implementation(int32 Phase,bool Passed)
{
#if !UE_BUILD_SHIPPING
    auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();auto* PS=ProfileState();auto* S=GetWorld()->GetGameState<AAetherFrontierState>();
    if(!ClosureServer()||!Mode||!PS||!S||Phase!=S->ClosurePhase)return;
    Mode->ClosureAcks.Add(PS->Profile.CharacterId,Phase);Mode->bClosureFailed|=!Passed;
    UE_LOG(LogTemp,Display,TEXT("V807_CLIENT_ACK %s id=%s phase=%d revision=%d"),Passed?TEXT("PASS"):TEXT("FAIL"),*PS->Profile.CharacterId,Phase,PS->Profile.Revision);
#endif
}
void AAetherFrontierCharacter::CheckClosureClient(float Dt)
{
#if !UE_BUILD_SHIPPING
    if(HasAuthority()||!IsLocallyControlled()||!ClosureClient())return;
    ClosureClientTime+=Dt;auto* S=GetWorld()->GetGameState<AAetherFrontierState>();auto* PS=ProfileState();
    if(!S||!PS||S->ClosurePhase==0||S->ClosurePhase==ClosureSeenPhase||ClosureClientTime<2)return;
    bool Private=true;for(TActorIterator<AAetherPlayerState> It(GetWorld());It;++It)if(*It!=PS)Private&=It->Profile.CharacterId.IsEmpty()&&It->Profile.Inventory.IsEmpty();
    auto Find=[&](FName Id)->AAetherFrontierProp*{for(TActorIterator<AAetherFrontierProp> It(GetWorld());It;++It)if(It->Spec.Id==Id)return *It;return nullptr;};
    auto* Source=Find("LabSource");auto* Bridge=Find("LabBridge");auto* Ice=Find("LabWater0");auto* Fire=Find("LabFire");auto* Crate=Find("LabCrate");
    if(!Source||!Bridge||!Ice||!Fire||!Crate)return;
    const auto* PC=Cast<APlayerController>(Controller);
    const bool HasLocalHUD=PC&&PC->GetHUD()&&PC->GetHUD()->GetClass()==AetherPresentation::ResolveHUD();
    bool Pass=Private&&HasLocalHUD&&S->bSupplyRestored&&!S->bPowerOn&&!Source->Mechanism->bPowerEnabled
        &&AbilitySystem==PS->AbilitySystem&&AbilitySystem->GetOwnerActor()==PS
        &&GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation()->GetStats().Registered==0
        &&(PS->Profile.CharacterId=="Alpha"?PS->Profile.Claims.Contains("Q_Main_03"):PS->Profile.Claims.IsEmpty());
    // 检查客户端收到的是真实 Spec 身份和等级，不以服务器本地日志代替复制证据。
    bool SkillSpecs=true;
    for(int32 Bit=0;Bit<4;++Bit)
    {
        const auto* Definition=FAetherSkillDefinitionsV10::Get().Legacy(Bit);
        const auto* Spec=Definition?AetherSkillBinding::Find(*AbilitySystem,Definition->SkillId):nullptr;
        const int32 Expected=PS->Profile.CharacterId=="Alpha"&&S->ClosurePhase!=100?(Bit==0?3:Bit==1?2:1):1;
        SkillSpecs&=Spec&&Spec->Level==Expected;
    }
    static bool RequestedSkill=false,ObservedSkillCost=false;
    if(SkillSpecs&&PS->Profile.CharacterId=="Alpha"&&S->ClosurePhase==1)
    {
        // 朝天空释放，不干扰闭环用例的持久化物理场景。
        if(!RequestedSkill&&Controller){Controller->SetControlRotation(FRotator(90,0,0));RequestedSkill=TrySkill(TEXT("Fire.Ignite"));}
        if(RequestedSkill&&Mana()<90)ObservedSkillCost=true;
        SkillSpecs&=ObservedSkillCost;
    }
    Pass&=SkillSpecs;
    // Late join observes durable physical states, not a server-only success marker.
    if(S->ClosurePhase==1)Pass&=Bridge->Mechanism->bReleased&&Ice->Reactive->IceSupport==EReactiveIceSupport::Bearing&&Fire->Reactive->State.bBurning;
    if(S->ClosurePhase==2)Pass&=!Crate->Carrier;
    if(S->ClosurePhase==100)Pass&=Bridge->Mechanism->bReleased&&FVector::Dist(Crate->GetActorLocation(),FVector(5100,5000,50))<25;
    // Allow normal initial property replication to catch up before reporting failure.
    if(!Pass&&ClosureClientTime<12)return;
    ClosureSeenPhase=S->ClosurePhase;ClosureClientTime=0;
    UE_LOG(LogTemp,Display,TEXT("V10_CLIENT_SKILL_SPECS %s remote_cost=%d"),SkillSpecs?TEXT("PASS"):TEXT("FAIL"),ObservedSkillCost);
    UE_LOG(LogTemp,Display,TEXT("V10_CLIENT_HUD %s"),HasLocalHUD?TEXT("PASS"):TEXT("FAIL"));
    UE_LOG(LogTemp,Display,TEXT("V807_CLIENT_STATE %s id=%s phase=%d revision=%d private=%d fire=%d ice=%d bridge=%d navversion=%u crate=(%.1f,%.1f,%.1f)"),Pass?TEXT("PASS"):TEXT("FAIL"),*PS->Profile.CharacterId,S->ClosurePhase,PS->Profile.Revision,Private,Fire->Reactive->State.bBurning,int(Ice->Reactive->IceSupport),Bridge->Mechanism->bReleased,Bridge->Traversal->Revision,Crate->GetActorLocation().X,Crate->GetActorLocation().Y,Crate->GetActorLocation().Z);
    ServerClosureAck(S->ClosurePhase,Pass);
#endif
}
void AAetherFrontierMode::CheckClosure()
{
#if !UE_BUILD_SHIPPING
    auto Check=[&](bool Pass,const TCHAR* Name){bClosureFailed|=!Pass;UE_LOG(LogTemp,Display,TEXT("V807_CASE %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),Name);};
    if(Elapsed>110){UE_LOG(LogTemp,Error,TEXT("V807_FAIL timeout stage=%d"),ClosureStage);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    AAetherFrontierCharacter* A=nullptr;AAetherFrontierCharacter* B=nullptr;
    for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(auto* PS=It->ProfileState())
    {if(PS->Profile.CharacterId=="Alpha")A=*It;if(PS->Profile.CharacterId=="Beta")B=*It;}
    auto* S=GetGameState<AAetherFrontierState>();auto* W=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();
    auto Move=[](AAetherFrontierCharacter* C,FVector P){C->SetBase(static_cast<UPrimitiveComponent*>(nullptr));C->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);C->GetCharacterMovement()->StopMovementImmediately();if(auto* PC=Cast<APlayerController>(C->Controller))PC->ClientSetLocation(P,PC->GetControlRotation());C->ForceNetUpdate();};
    auto Act=[](AAetherFrontierCharacter* C,FName Action,FVector Target){const auto Look=(Target-C->GetActorLocation()-FVector(0,0,25)).Rotation();C->Controller->SetControlRotation(Look);C->NextServerAction=0;C->ClientClosureAction(Action,Look);};
    auto Next=[&](int Stage){ClosureStage=Stage;ClosureAt=Elapsed;};
    auto Acked=[&](int Phase){return ClosureAcks.FindRef("Alpha")==Phase&&ClosureAcks.FindRef("Beta")==Phase;};
    const bool Reload=FParse::Param(FCommandLine::Get(),TEXT("AetherV807Reload"));
    if(Reload)
    {
        if(!A||!B)return;
        S->ClosurePhase=100;
        if(!Acked(100))return;
        const auto* Loot=Database->Loot.FindByPredicate([](const auto& L){return L.ClaimId==LootReceipt;});
        Check(Loot&&!Loot->ClaimedBy.IsEmpty()&&A->ProfileState()->Profile.Count("Material")+B->ProfileState()->Profile.Count("Material")==2,TEXT("AUD8-23 restart reward receipt and quantity"));
        Check(A->ProfileState()->Profile.InventoryReceipts.Num()==1&&B->ProfileState()->Profile.InventoryReceipts.Num()==1,TEXT("V9 remote inventory receipts survive server restart"));
        Check(Database->bSupplyRestored&&!Database->bPowerOn&&Database->ServiceReceipts.Num()==1&&Database->WorldFacts.Sources.Contains("SupplyRestored"),TEXT("AUD8-23 restart world transaction"));
        UE_LOG(LogTemp,Display,TEXT("V807_RELOAD_%s generation=%d receipt=%s"),bClosureFailed?TEXT("FAIL"):TEXT("PASS"),Database->Generation,*LootReceipt.ToString());
        A->ClientClosureAction("Disconnect",FRotator::ZeroRotator);B->ClientClosureAction("Disconnect",FRotator::ZeroRotator);FPlatformMisc::RequestExitWithStatus(false,bClosureFailed?1:0);return;
    }
    if(ClosureStage==0)
    {
        if(!A||A->bTravelPending||Elapsed<3)return;
        Check(!FModuleManager::Get().IsModuleLoaded("AetherUI"),TEXT("V10 dedicated process excludes UI module"));
        auto P=A->ProfileState()->Profile;
        for(FName Q:{FName("Q_Main_01"),FName("Q_Main_02"),FName("Q_Main_03")}){for(FName O:FAetherProfile::Objectives(Q))P.Observe(O);P.Claim(Q);}
        P.LearnedSpells=15;Check(Commit(A->ProfileState(),P),TEXT("fixture advanced Alpha only"));
        // 仅该独立合成夹具设置等级；正式玩家升级仍必须经后续持久事务入口。
        for(const auto& Pair:TMap<FString,int32>{{TEXT("Fire.Ignite"),3},{TEXT("Water.Draw"),2}})
        {
            auto* Spec=AetherSkillBinding::Find(*A->AbilitySystem,Pair.Key);
            Check(Spec!=nullptr,TEXT("V10 authoritative stable skill spec"));
            if(Spec){Spec->Level=Pair.Value;A->AbilitySystem->MarkAbilitySpecDirty(*Spec);}
        }
        Move(A,Prop("Pump")->GetActorLocation()+FVector(0,-160,100));
        Next(1);return;
    }
    if(ClosureStage==1)
    {
        FAetherWorldServiceCommand Command;Command.Id=FGuid(0xA375E807,0,0,1);Command.TargetId="Pump";Command.ExpectedRevision=A->ProfileState()->Profile.Revision;
        const auto Result=ExecuteWorldService(A,Command);if(Result==EAetherServiceResult::Busy)return;
        Check(Result==EAetherServiceResult::Committed,TEXT("shared supply transaction"));
        S->bPowerOn=false;Prop("PowerSource")->Mechanism->bPowerEnabled=false;Prop("LabSource")->Mechanism->bPowerEnabled=false;
        FReactiveStimulus Freeze;Freeze.HeatJ=-260000;Prop("LabWater0")->Reactive->Inject(Freeze);
        FReactiveStimulus Cut;Cut.CuttingWorkJ=10000;Prop("LabRope")->Reactive->Inject(Cut);
        Move(A,FVector(4900,5100,110));Next(2);return;
    }
    if(ClosureStage==2)
    {
        if(Elapsed-ClosureAt<3||!SaveWorld())return;
        S->ClosurePhase=1;UE_LOG(LogTemp,Display,TEXT("V807_LATE_READY generation=%d"),Database->Generation);Next(3);return;
    }
    if(ClosureStage==3)
    {
        if(!A||!B||!Acked(1))return;
        Check(A->ProfileState()->Profile.Claims.Contains("Q_Main_03")&&B->ProfileState()->Profile.Claims.IsEmpty()&&!B->ProfileState()->Profile.Evidence.Contains("SupplyRestored"),TEXT("AUD8-21 different quest progress"));
        auto* Fire=Make("ClosurePrivate","TrainingExtinguished",FVector(4800,4600,50),FVector(.6),EAetherObjectKind::Timber,TEXT(""));Fire->SetOwner(A);
        FReactiveStimulus Hit;Hit.SourceActor=B;Hit.HeatJ=60000;Check(!Fire->Reactive->Inject(Hit),TEXT("AUD8-21 foreign private stimulus rejected"));
        Hit.SourceActor=A;Check(Fire->Reactive->Inject(Hit),TEXT("AUD8-21 owner private stimulus accepted"));
        ClosureBuddy=SpawnFighter(FVector(4800,4900,110),EAetherFighter::Player,"ClosureBuddy");ClosureBuddy->CompanionOwner=A;Companions.Add(ClosureBuddy.Get());
        Act(B,"PartyCommand",B->GetActorLocation());Next(4);return;
    }
    if(ClosureStage==4)
    {
        if(Elapsed-ClosureAt<1)return;Check(ClosureBuddy.IsValid()&&!ClosureBuddy->bCompanionHold,TEXT("AI ignores another player's command"));
        Act(A,"PartyCommand",A->GetActorLocation());Next(5);return;
    }
    if(ClosureStage==5)
    {
        if(Elapsed-ClosureAt<1)return;Check(ClosureBuddy.IsValid()&&ClosureBuddy->bCompanionHold,TEXT("AI accepts owner's remote command"));
        auto* Candidate=DuplicateObject<UAetherFrontierSave>(Database,this);FAetherWorldLoot Loot;Loot.ClaimId=LootReceipt;Loot.Count=2;Loot.Location=FVector(5100,5200,40);Candidate->Loot.Add(Loot);
        Check(WriteDatabase(Candidate),TEXT("shared loot fixture durable"));SpawnLoot(Loot);
        ClosureMaterialTotal=A->ProfileState()->Profile.Count("Material")+B->ProfileState()->Profile.Count("Material");
        Move(A,FVector(4960,5200,100));Move(B,FVector(5240,5200,100));Next(6);return;
    }
    if(ClosureStage==6)
    {
        if(Elapsed-ClosureAt<1)return;Act(A,"Interact",FVector(5100,5200,40));Act(B,"Interact",FVector(5100,5200,40));Next(7);return;
    }
    if(ClosureStage==7)
    {
        if(Elapsed-ClosureAt<1)return;
        const auto* Loot=Database->Loot.FindByPredicate([](const auto& L){return L.ClaimId==LootReceipt;});
        Check(!Prop(LootName)&&Loot&&!Loot->ClaimedBy.IsEmpty()&&A->ProfileState()->Profile.Count("Material")+B->ProfileState()->Profile.Count("Material")==ClosureMaterialTotal+2,TEXT("AUD8-20 two remote requests one loot receipt"));
        UE_LOG(LogTemp,Display,TEXT("V807_RECEIPT id=%s owner=%s generation=%d"),*LootReceipt.ToString(),Loot?*Loot->ClaimedBy:TEXT("missing"),Database->Generation);
        ClaimLoot(A,LootName);ClaimLoot(B,LootName);
        auto* Crate=Prop("LabCrate");Crate->SetActorLocation(FVector(5100,5000,50),false,nullptr,ETeleportType::TeleportPhysics);Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Move(A,FVector(4960,5000,100));Move(B,FVector(5240,5000,100));Next(8);return;
    }
    if(ClosureStage==8)
    {
        if(Elapsed-ClosureAt<1)return;const auto P=Prop("LabCrate")->GetActorLocation();Act(A,"Carry",P);Act(B,"Carry",P);Next(9);return;
    }
    if(ClosureStage==9)
    {
        if(Elapsed-ClosureAt<.7)return;auto* Crate=Prop("LabCrate");
        Check((A->Carried==Crate)!=(B->Carried==Crate),TEXT("AUD8-20 one carrier after two remote requests"));
        Check(Crate->Carrier&&(Crate->Carrier==A||Crate->Carrier==B),TEXT("AUD8-20 prop carrier consistent"));
        A->ReleaseCarry();B->ReleaseCarry();Move(A,FVector(4900,5200,110));
        Crate->SetActorLocation(FVector(5100,5000,50),false,nullptr,ETeleportType::TeleportPhysics);Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);Move(B,FVector(5240,5000,100));Next(10);return;
    }
    if(ClosureStage==10)
    {
        if(Elapsed-ClosureAt<1)return;Act(B,"Carry",Prop("LabCrate")->GetActorLocation());Next(11);return;
    }
    if(ClosureStage==11)
    {
        if(Elapsed-ClosureAt<.7)return;Check(B->Carried==Prop("LabCrate"),TEXT("disconnect while carrying fixture"));
        B->ClientClosureAction("Disconnect",FRotator::ZeroRotator);Next(12);return;
    }
    if(ClosureStage==12)
    {
        if(B)return;Check(!Prop("LabCrate")->Carrier,TEXT("AUD8-23 voluntary disconnect releases carry"));
        if(!SaveWorld())return;S->ClosurePhase=2;UE_LOG(LogTemp,Display,TEXT("V807_RECONNECT_READY generation=%d"),Database->Generation);Next(13);return;
    }
    if(ClosureStage==13)
    {
        if(!B||!Acked(2))return;const auto* Loot=Database->Loot.FindByPredicate([](const auto& L){return L.ClaimId==LootReceipt;});
        Check(Loot&&!Loot->ClaimedBy.IsEmpty()&&A->ProfileState()->Profile.Count("Material")+B->ProfileState()->Profile.Count("Material")==ClosureMaterialTotal+2,TEXT("AUD8-23 rejoin rewards remain unique"));
        auto* Crate=Prop("LabCrate");Crate->SetActorLocation(FVector(5100,5000,50),false,nullptr,ETeleportType::TeleportPhysics);Crate->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);Next(14);return;
    }
    if(ClosureStage==14)
    {
        if(Elapsed-ClosureAt<1)return;
        Act(A,"InventoryProbe",A->GetActorLocation());Act(B,"InventoryProbe",B->GetActorLocation());Next(16);return;
    }
    if(ClosureStage==16)
    {
        if(Elapsed-ClosureAt<1)return;
        for(auto* C:{A,B})Check(C->ProfileState()->Profile.InventoryReceipts.Num()==1&&C->ProfileState()->Profile.InventoryReceipts[0].Transferred==1,TEXT("V9 remote duplicate split one receipt"));
        Act(A,"InventoryStale",A->GetActorLocation());Act(B,"InventoryStale",B->GetActorLocation());Next(17);return;
    }
    if(ClosureStage==17)
    {
        if(Elapsed-ClosureAt<1||!SaveWorld())return;
        for(auto* C:{A,B})Check(C->ProfileState()->Profile.InventoryReceipts.Num()==1,TEXT("V9 remote stale command has no second mutation"));
        UE_LOG(LogTemp,Display,TEXT("V807_SESSION_%s generation=%d correction=authoritative/no-prediction"),bClosureFailed?TEXT("FAIL"):TEXT("PASS"),Database->Generation);
        A->ClientClosureAction("Disconnect",FRotator::ZeroRotator);B->ClientClosureAction("Disconnect",FRotator::ZeroRotator);Next(15);return;
    }
    if(ClosureStage==15&&Elapsed-ClosureAt>1)FPlatformMisc::RequestExitWithStatus(false,bClosureFailed?1:0);
#endif
}
