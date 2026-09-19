#include "../AetherFrontier.h"
#include "AetherGuide.h"
#include "AetherTradeNetworkProbe.h"
#include "Movement/AetherCharacterMovement.h"
#include "Movement/AetherDodgeAbility.h"
#include "Components/CapsuleComponent.h"
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
bool TradeNetwork(){return FParse::Param(FCommandLine::Get(),TEXT("AetherV10TradeNetwork"));}
// 探针只在显式开发测试开关下记录客户端收到的回执，不替代服务器授权或持久账本。
bool MovementNetwork(){return FParse::Param(FCommandLine::Get(),TEXT("AetherV10MovementNetwork"));}
struct FMovementNetworkProbe
{
    int32 RequestedPhase=0;float JumpAt=0,DodgeAt=0,MinimumStamina=100;
    bool SawOwnAir=false,SawOtherAir=false,PredictedDodge=false,DuplicateRejected=false;
    bool DodgeFell=false,RollbackObserved=false;FVector DodgeStart=FVector::ZeroVector;
};
FMovementNetworkProbe MovementProbe;
bool ServerSawJumpA=false,ServerSawJumpB=false;
bool ServerDodgeFell=false;float ServerMinimumStaminaA=100,ServerMinimumStaminaB=100;
struct FTradeNetworkProbe
{
    FGuid Token;FAetherInventoryCommand Purchase;FGuid InFlight;
    int32 RequestedPhase=0;bool bResultReceived=false;EAetherInventoryResult Result=EAetherInventoryResult::InvalidCommand;
};
TMap<TWeakObjectPtr<AAetherFrontierCharacter>,FTradeNetworkProbe> TradeProbes;
const FGuid LootReceipt(0xA375E807,0,0,20);
const FName LootName=*FString("Loot_"+LootReceipt.ToString(EGuidFormats::Digits));
}
void AetherTradeNetwork::ObserveResult(AAetherFrontierCharacter* C,FGuid Command,EAetherInventoryResult Result)
{
#if !UE_BUILD_SHIPPING
    if(!TradeNetwork()||!ClosureClient())return;
    if(auto* Probe=TradeProbes.Find(C);Probe&&Probe->InFlight==Command){Probe->Result=Result;Probe->bResultReceived=true;}
#endif
}
void AAetherFrontierCharacter::ClientClosureAction_Implementation(FName Action,FRotator Look)
{
#if !UE_BUILD_SHIPPING
    if(!ClosureClient()||!IsLocallyControlled())return;
    if(Action=="Disconnect"){UE_LOG(LogTemp,Display,TEXT("V807_VOLUNTARY_DISCONNECT"));FPlatformMisc::RequestExit(false);return;}
    if(Controller)Controller->SetControlRotation(Look);
    UE_LOG(LogTemp,Display,TEXT("V807_CLIENT_ACTION id=%s action=%s"),ProfileState()?*ProfileState()->Profile.CharacterId:TEXT("pending"),*Action.ToString());
    if(MovementNetwork()&&Action.ToString().StartsWith(TEXT("Move")))
    {
        if(Action=="MoveStand"){ReleaseHeldInput();MovementProbe={};MovementProbe.RequestedPhase=1;}
        else if(Action=="MoveCrouch"){SetCrouchInput(true);MovementProbe.RequestedPhase=2;}
        else if(Action=="MoveSprint"){SetCrouchInput(false);SetSprintInput(true);MovementProbe.RequestedPhase=3;}
        else if(Action=="MoveJump")
        {
            SetSprintInput(false);StartJumpInput();MovementProbe.RequestedPhase=4;
            MovementProbe.JumpAt=GetWorld()->GetTimeSeconds();
        }
        else if(Action=="MoveDodge"||Action=="MoveDodgeDenied")
        {
            ReleaseHeldInput();MovementProbe.RequestedPhase=Action=="MoveDodge"?6:7;
            MovementProbe.DodgeStart=GetActorLocation();MovementProbe.DodgeAt=GetWorld()->GetTimeSeconds();
            MovementProbe.MinimumStamina=100;MovementProbe.DodgeFell=false;MovementProbe.RollbackObserved=false;
            // 故意模拟尚未收到眩晕复制的客户端；服务器状态不改，必须拒绝这次预测。
            if(Action=="MoveDodgeDenied")StunUntil=0;
            MovementProbe.PredictedDodge=TryDodge()&&GetCharacterMovement()->GetRootMotionSource(TEXT("Aether.Dodge")).IsValid();
            MovementProbe.DuplicateRejected=!TryDodge();
            MovementProbe.MinimumStamina=Stamina();
        }
        else if(Action=="MoveFlush")
        {
            bAttackHeld=true;Jump();SetSprintInput(true);
            if(auto* PC=Cast<APlayerController>(Controller))PC->FlushPressedKeys();
            MovementProbe.RequestedPhase=5;
        }
        return;
    }
    if(TradeNetwork()&&Action.ToString().StartsWith(TEXT("Trade")))
    {
        for(auto It=TradeProbes.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
        if(TradeProbes.Num()>=16&&!TradeProbes.Contains(this))return;
        auto& Probe=TradeProbes.FindOrAdd(this);auto* PS=ProfileState();if(!PS)return;
        if(Action=="TradeOpen"){RefreshInteractionFocus();InteractV4();return;}
        if(Action=="TradeClose"){CloseTrade();Probe.RequestedPhase=3;return;}
        if(Action=="TradeBuy")
        {
            Probe.Token=TradeSession.Token;Probe.Purchase={};auto& C=Probe.Purchase;
            C.CommandId=FGuid::NewGuid();C.Action="Buy";C.ShopId=ActiveShop();C.DefinitionId="Potion";C.Quantity=2;C.ExpectedInventoryRevision=PS->Profile.Revision;
            Probe.RequestedPhase=2;PendingInventory=C;
        }
        // 关闭会话后仍重放原始字节；只有新请求才更换命令 ID 和版本。
        else if(Action=="TradeReplay"){Probe.RequestedPhase=4;PendingInventory=Probe.Purchase;}
        else if(Action=="TradeExpired")
        {
            Probe.RequestedPhase=5;PendingInventory=Probe.Purchase;
            PendingInventory.CommandId=FGuid::NewGuid();PendingInventory.ExpectedInventoryRevision=PS->Profile.Revision;
        }
        else return;
        PendingTradeAuthorization=Probe.Token;Probe.InFlight=PendingInventory.CommandId;Probe.bResultReceived=false;
        const auto Request=PendingInventory;
        ServerTradeInventory(Request,Probe.Token);
        if(Action=="TradeBuy")ServerTradeInventory(Request,Probe.Token); // 真实重复网络投递。
        return;
    }
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
    if(Action=="Interact"){RefreshInteractionFocus();InteractV4();return;}
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
    if(MovementNetwork()&&S&&PS)
    {
        // 运动输入逐帧驱动；短暂腾空必须逐帧采样，不能在两秒后的静态快照中推断。
        if(MovementProbe.RequestedPhase==3)AddMovementInput(FVector(1,0,0));
        if(MovementProbe.RequestedPhase==6||MovementProbe.RequestedPhase==7)
        {
            MovementProbe.MinimumStamina=FMath::Min(MovementProbe.MinimumStamina,Stamina());
            MovementProbe.DodgeFell|=GetCharacterMovement()->IsFalling();
            if(MovementProbe.RequestedPhase==7&&GetWorld()->GetTimeSeconds()-MovementProbe.DodgeAt<.5f)
                MovementProbe.RollbackObserved|=!AbilitySystem->HasMatchingGameplayTag(AetherDodge::ActiveTag())&&Stamina()>=99;
        }
        if(MovementProbe.RequestedPhase==4)
        {
            if(GetWorld()->GetTimeSeconds()-MovementProbe.JumpAt>.12f)StopJumping();
            MovementProbe.SawOwnAir|=GetCharacterMovement()->IsFalling()&&GetActorLocation().Z>1170;
            for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)
                if(*It!=this&&It->GetPlayerState()&&It->Fighter==EAetherFighter::Player)
                    MovementProbe.SawOtherAir|=It->GetCharacterMovement()->IsFalling()&&It->GetActorLocation().Z>1170;
        }
    }
    if(!S||!PS||S->ClosurePhase==0||S->ClosurePhase==ClosureSeenPhase||ClosureClientTime<2)return;
    bool Private=true;for(TActorIterator<AAetherPlayerState> It(GetWorld());It;++It)if(*It!=PS)Private&=It->Profile.CharacterId.IsEmpty()&&It->Profile.Inventory.IsEmpty();
    if(MovementNetwork())
    {
        auto* M=CastChecked<UAetherCharacterMovement>(GetCharacterMovement());const int32 Phase=S->ClosurePhase;
        bool Pass=Private&&MovementProbe.RequestedPhase==Phase&&M->IsMovingOnGround();
        if(Phase==1)Pass&=!IsCrouched()&&GetActorLocation().Z>1100;
        else if(Phase==2)
        {
            Pass&=IsCrouched()&&GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()==48&&!bSprinting;
            bool OtherCrouched=false;
            for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)
                if(*It!=this&&It->GetPlayerState()&&It->Fighter==EAetherFighter::Player)OtherCrouched|=It->IsCrouched();
            Pass&=OtherCrouched;
        }
        else if(Phase==3)Pass&=bSprinting&&!IsCrouched()&&GetVelocity().Size2D()>500&&GetActorLocation().X>8800;
        else if(Phase==4)Pass&=MovementProbe.SawOwnAir&&MovementProbe.SawOtherAir&&!bPressedJump;
        else if(Phase==5)Pass&=!M->bWantsSprint&&!bSprinting&&!bPressedJump&&!bAttackHeld;
        else if(Phase==6)Pass&=MovementProbe.PredictedDodge&&MovementProbe.DuplicateRejected&&!MovementProbe.DodgeFell&&MovementProbe.MinimumStamina<=83&&GetActorLocation().X-MovementProbe.DodgeStart.X>190&&GetActorLocation().X-MovementProbe.DodgeStart.X<280;
        else if(Phase==7)Pass&=MovementProbe.PredictedDodge&&MovementProbe.RollbackObserved&&Stamina()>=99&&FVector::Dist2D(GetActorLocation(),MovementProbe.DodgeStart)<30;
        else Pass=false;
        if(!Pass&&ClosureClientTime<12)return;
        ClosureSeenPhase=Phase;ClosureClientTime=0;
        UE_LOG(LogTemp,Display,TEXT("V10_MOVEMENT_CLIENT_%s id=%s phase=%d crouch=%d sprint=%d x=%.1f z=%.1f own_air=%d remote_air=%d predicted=%d rollback=%d minimum_stamina=%.1f dodge_dx=%.1f"),
            Pass?TEXT("PASS"):TEXT("FAIL"),*PS->Profile.CharacterId,Phase,IsCrouched(),bSprinting,GetActorLocation().X,GetActorLocation().Z,MovementProbe.SawOwnAir,MovementProbe.SawOtherAir,MovementProbe.PredictedDodge,MovementProbe.RollbackObserved,MovementProbe.MinimumStamina,GetActorLocation().X-MovementProbe.DodgeStart.X);
        ServerClosureAck(Phase,Pass);return;
    }
    if(TradeNetwork())
    {
        auto* Probe=TradeProbes.Find(this);const int32 Phase=S->ClosurePhase;
        bool Pass=Private;const auto* PC=Cast<APlayerController>(Controller);
        Pass&=PC&&PC->GetHUD()&&AbilitySystem==PS->AbilitySystem&&AbilitySystem->GetAvatarActor()==this;
        if(Phase==1)Pass&=!ActiveShop().IsNone()&&TradeSession.Token.IsValid()&&bPanel&&Panel==1&&PS->Profile.Gold==100&&PS->Profile.Count("Potion")==0;
        else if(Phase==3)Pass&=Probe&&Probe->RequestedPhase==3&&!TradeSession.Token.IsValid();
        else if(Phase==2||Phase==4||Phase==5)
        {
            const auto Expected=Phase==5?EAetherInventoryResult::OutOfReach:EAetherInventoryResult::Applied;
            Pass&=Probe&&Probe->RequestedPhase==Phase&&Probe->bResultReceived&&Probe->Result==Expected&&!PendingInventory.CommandId.IsValid();
        }
        else Pass=false;
        if(Phase>=2)Pass&=PS->Profile.Gold==60&&PS->Profile.Count("Potion")==2&&PS->Profile.InventoryReceipts.Num()==1;
        if(!Pass&&ClosureClientTime<12)return;
        ClosureSeenPhase=Phase;ClosureClientTime=0;
        UE_LOG(LogTemp,Display,TEXT("V10_TRADE_CLIENT_%s id=%s phase=%d gold=%d potion=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),*PS->Profile.CharacterId,Phase,PS->Profile.Gold,PS->Profile.Count("Potion"));
        ServerClosureAck(Phase,Pass);return;
    }
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
    auto Move=[](AAetherFrontierCharacter* C,FVector P){C->SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));C->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);C->GetCharacterMovement()->StopMovementImmediately();if(auto* PC=Cast<APlayerController>(C->Controller))PC->ClientSetLocation(P,PC->GetControlRotation());C->ForceNetUpdate();};
    auto Act=[](AAetherFrontierCharacter* C,FName Action,FVector Target){const auto Look=(Target-C->GetActorLocation()-FVector(0,0,25)).Rotation();C->Controller->SetControlRotation(Look);C->NextServerAction=0;C->ClientClosureAction(Action,Look);};
    auto Next=[&](int Stage){ClosureStage=Stage;ClosureAt=Elapsed;};
    auto Acked=[&](int Phase){return ClosureAcks.FindRef("Alpha")==Phase&&ClosureAcks.FindRef("Beta")==Phase;};
    if(MovementNetwork())
    {
        if(ClosureStage==10){if(Elapsed-ClosureAt>.5)FPlatformMisc::RequestExitWithStatus(false,bClosureFailed?1:0);return;}
        if(!A||!B||A->bTravelPending||B->bTravelPending)return;
        if(ClosureStage==0)
        {
            // 使用正式可复制的基础几何体搭建短测试地面；不进入个人存档。
            Make("V10MoveFloor",NAME_None,FVector(10000,10000,1000),FVector(60,40,1),EAetherObjectKind::Stone,TEXT(""));
            for(auto* C:{A,B}){C->ResetCombat();C->SetVitals(100,100,100);}
            Move(A,FVector(8500,9800,1140));Move(B,FVector(8500,10200,1140));
            ServerSawJumpA=ServerSawJumpB=false;Next(1);return;
        }
        auto Send=[&](int32 Phase,FName Action)
        {
            S->ClosurePhase=Phase;S->ForceNetUpdate();
            Act(A,Action,A->GetActorLocation()+FVector(100,0,25));Act(B,Action,B->GetActorLocation()+FVector(100,0,25));
        };
        if(ClosureStage==1&&Elapsed-ClosureAt>1){Send(1,"MoveStand");Next(2);return;}
        if(ClosureStage==2&&Acked(1)){Send(2,"MoveCrouch");Next(3);return;}
        if(ClosureStage==3&&Acked(2))
        {
            Check(A->IsCrouched()&&B->IsCrouched(),TEXT("remote saved moves crouch authoritative capsules"));
            Send(3,"MoveSprint");Next(4);return;
        }
        if(ClosureStage==4&&Acked(3))
        {
            for(auto* C:{A,B})Check(C->bSprinting&&!C->IsCrouched()&&C->GetActorLocation().X>8800&&C->Stamina()<99,TEXT("remote sprint movement and authoritative stamina cost"));
            Send(4,"MoveJump");Next(5);return;
        }
        if(ClosureStage==5)
        {
            ServerSawJumpA|=A->GetCharacterMovement()->IsFalling()&&A->GetActorLocation().Z>1170;
            ServerSawJumpB|=B->GetCharacterMovement()->IsFalling()&&B->GetActorLocation().Z>1170;
            if(Acked(4)){Check(ServerSawJumpA&&ServerSawJumpB,TEXT("server observed both real jumps"));Send(5,"MoveFlush");Next(6);}return;
        }
        if(ClosureStage==6&&Acked(5))
        {
            Check(!A->bSprinting&&!B->bSprinting,TEXT("remote flush stops authoritative sprint"));
            for(auto* C:{A,B})C->SetVitals(100,100,100);
            ServerMinimumStaminaA=ServerMinimumStaminaB=100;ServerDodgeFell=false;
            Send(6,"MoveDodge");Next(7);return;
        }
        if(ClosureStage==7)
        {
            ServerMinimumStaminaA=FMath::Min(ServerMinimumStaminaA,A->Stamina());
            ServerMinimumStaminaB=FMath::Min(ServerMinimumStaminaB,B->Stamina());
            ServerDodgeFell|=A->GetCharacterMovement()->IsFalling()||B->GetCharacterMovement()->IsFalling();
            if(Acked(6))
            {
                Check(!ServerDodgeFell&&ServerMinimumStaminaA<=86&&ServerMinimumStaminaB<=86,TEXT("predicted dodge stays grounded and charges on server"));
                for(auto* C:{A,B}){C->SetVitals(100,100,100);C->StunUntil=C->CombatTime()+5;}
                Send(7,"MoveDodgeDenied");Next(8);
            }
            return;
        }
        if(ClosureStage==8&&Acked(7))
        {
            Check(A->Stamina()>=99&&B->Stamina()>=99,TEXT("server denied prediction charges no stamina"));
            UE_LOG(LogTemp,Display,TEXT("V10_MOVEMENT_NETWORK_%s"),bClosureFailed?TEXT("FAIL"):TEXT("PASS"));
            A->ClientClosureAction("Disconnect",FRotator::ZeroRotator);B->ClientClosureAction("Disconnect",FRotator::ZeroRotator);Next(10);return;
        }
        return;
    }
    if(TradeNetwork())
    {
        // 客户端退出后角色可能已销毁；收尾不能依赖两名角色仍然存在。
        if(ClosureStage==7){if(Elapsed-ClosureAt>.5)FPlatformMisc::RequestExitWithStatus(false,bClosureFailed?1:0);return;}
        if(!A||!B||A->bTravelPending||B->bTravelPending)return;
        auto* Merchant=Prop("Shop");if(!Merchant)return;
        if(ClosureStage==0)
        {
            for(auto* C:{A,B})
            {
                auto P=C->ProfileState()->Profile;P.Gold=100;P.Inventory.Reset();P.Equipped.Reset();
                Check(Commit(C->ProfileState(),P),TEXT("trade network isolated fixture"));C->ResetCombat();C->ApplyProfileEquipment();
            }
            Move(A,Merchant->GetActorLocation()+FVector(-140,-90,40));Move(B,Merchant->GetActorLocation()+FVector(140,-90,40));
            S->ClosurePhase=1;S->ForceNetUpdate();Next(1);return;
        }
        if(ClosureStage==1&&Elapsed-ClosureAt>.7)
        {Act(A,"TradeOpen",Merchant->GetActorLocation());Act(B,"TradeOpen",Merchant->GetActorLocation());Next(2);return;}
        if(ClosureStage==2&&Acked(1))
        {
            Check(A->TradeSession.Token.IsValid()&&B->TradeSession.Token.IsValid()&&A->TradeSession.Token!=B->TradeSession.Token,TEXT("distinct remote merchant leases"));
            S->ClosurePhase=2;S->ForceNetUpdate();Act(A,"TradeBuy",Merchant->GetActorLocation());Act(B,"TradeBuy",Merchant->GetActorLocation());Next(3);return;
        }
        if(ClosureStage==3&&Acked(2))
        {S->ClosurePhase=3;S->ForceNetUpdate();Act(A,"TradeClose",Merchant->GetActorLocation());Act(B,"TradeClose",Merchant->GetActorLocation());Next(4);return;}
        if(ClosureStage==4&&Acked(3))
        {
            Check(!A->TradeSession.Token.IsValid()&&!B->TradeSession.Token.IsValid(),TEXT("remote close revokes server authorization"));
            S->ClosurePhase=4;S->ForceNetUpdate();Act(A,"TradeReplay",Merchant->GetActorLocation());Act(B,"TradeReplay",Merchant->GetActorLocation());Next(5);return;
        }
        if(ClosureStage==5&&Acked(4))
        {S->ClosurePhase=5;S->ForceNetUpdate();Act(A,"TradeExpired",Merchant->GetActorLocation());Act(B,"TradeExpired",Merchant->GetActorLocation());Next(6);return;}
        if(ClosureStage==6&&Acked(5))
        {
            for(auto* C:{A,B})Check(C->ProfileState()->Profile.Gold==60&&C->ProfileState()->Profile.Count("Potion")==2&&C->ProfileState()->Profile.InventoryReceipts.Num()==1,TEXT("remote retries debit once and expired new request rejected"));
            Check(!FModuleManager::Get().IsModuleLoaded("AetherUI"),TEXT("dedicated trade process excludes client UI"));
            UE_LOG(LogTemp,Display,TEXT("V10_TRADE_NETWORK_%s"),bClosureFailed?TEXT("FAIL"):TEXT("PASS"));
            A->ClientClosureAction("Disconnect",FRotator::ZeroRotator);B->ClientClosureAction("Disconnect",FRotator::ZeroRotator);Next(7);return;
        }
        return;
    }
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
