#include "AetherFrontier.h"
#include "Framework/AetherPlayerController.h"
#include "World/AetherNativeContainer.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "Inventory/AetherResourceGate.h"
#include "Definitions/AetherV10Definitions.h"
#include "EngineUtils.h"

namespace
{
bool Reachable(AAetherFrontierCharacter& Player,AActor& Target,double Radius)
{
    if(Target.GetWorld()!=Player.GetWorld()||Target.IsActorBeingDestroyed()||
        FVector::DistSquared(Player.GetActorLocation(),Target.GetActorLocation())>FMath::Square(Radius))return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(NativeCommandReach),false,&Player);Q.AddIgnoredActor(&Target);
    return !Player.GetWorld()->LineTraceTestByChannel(Player.GetActorLocation(),Target.GetActorLocation(),ECC_Visibility,Q);
}
}
bool AAetherFrontierMode::ResolveNativeContext(AAetherPlayerController& PC,const FAetherPlayerCommand& Command,
    const FAetherProfileStateV10& Profile,FAetherProfileCommandContext& X)
{
    auto* C=Cast<AAetherFrontierCharacter>(PC.GetPawn());auto* PS=PC.GetPlayerState<AAetherPlayerState>();
    if(!bNativeSceneReady||bWorldRestoreFailed||!C||!PS||C->ProfileState()!=PS||!C->HasAuthority()||
        !Profile.CharacterId.Equals(PS->Profile.CharacterId,ESearchCase::CaseSensitive))return false;
    X={};const auto& D=FAetherV10Definitions::Get();if(!D.bValid)return false;
    const bool Busy=C->bTravelPending||C->Carried||C->ReviveTarget||(Encounters&&Encounters->IsChanneling(C));
    X.bCanManageInventory=C->Ready()&&!Busy&&C->ResourceGate&&!C->ResourceGate->IsBlocked();
    if(!X.bCanManageInventory)return false;
    const float Now=C->CombatTime();const bool Combat=C->HasRecentCombat(8);
    bool Threatened=false;
    for(TActorIterator<AAetherCharacter> It(GetWorld());It;++It)
        if(It->Fighter!=EAetherFighter::Player&&It->Alive()&&FVector::DistSquared(C->GetActorLocation(),It->GetActorLocation())<FMath::Square(1800.)){Threatened=true;break;}
    X.SafeForSeconds=Combat||Threatened?0:FMath::Max(8.f,C->TimeSinceDamage());
    X.Skill.CharacterLevel=1+Profile.Experience/200;X.Skill.bInCombat=Combat||Threatened;
    X.Skill.bCasting=C->CastLockUntil>Now;X.Skill.bCoolingDown=X.Skill.bCasting;
    for(const auto& Claim:Profile.Claims)X.Skill.CompletedQuests.Add(Claim);
    X.ExternalSkillGrants=PS->GetNativeSkillGrants();
    const auto* Registry=GetWorld()->GetSubsystem<UAetherNearbyRegistry>();
    auto* Target=Command.TargetStableId.IsEmpty()?nullptr:Prop(FName(*Command.TargetStableId));
    // FName 查询不区分大小写；协议的稳定身份仍须逐字匹配，不能接受大小写别名。
    if(Target&&(!Target->Spec.Id.ToString().Equals(Command.TargetStableId,ESearchCase::CaseSensitive)||!Registry||!Registry->Contains(Target)))Target=nullptr;
    auto& I=X.Interaction;I.CharacterId=Profile.CharacterId;I.ProfileRevision=Profile.Revision;
    I.WorldRevision=NativeWorld.IsSet()?NativeWorld->Revision:-1;I.bActorCanAct=X.bCanManageInventory;
    I.bBusy=Busy;I.bDowned=!C->Alive();I.bInCombat=Combat;I.bThreatened=Threatened;
    if(Target)
    {
        I.TargetStableId=Target->Spec.Id.ToString();I.DefinitionId=Target->Service.ToString();I.InteractionRevision=Target->InteractionRevision;
        I.bLoaded=Target->bEnabled&&!Target->IsActorBeingDestroyed();
        I.bInRange=FVector::DistSquared(C->GetActorLocation(),Target->GetActorLocation())<=FMath::Square(250.);
        I.bLineOfSight=Reachable(*C,*Target,250.);
        if(Target->Reactive->bOwnerOnlyStimuli)
        {auto* Owner=Cast<AAetherFrontierCharacter>(Target->GetOwner());I.OwnerCharacterId=Owner&&Owner->ProfileState()?Owner->ProfileState()->Profile.CharacterId:TEXT("UnavailableOwner");}
    }
    for(const auto& Id:Profile.Claims)I.Claims.Add(Id);for(const auto& Id:Profile.Evidence)I.Evidence.Add(Id);
    // 重置不从客户端声明服务资格；只在服务器实际存在、可达的导师附近开放。
    if(!Combat&&!Threatened)
        for(const auto& P:Props)if(IsValid(P)&&P->Service=="Teacher"&&P->bEnabled&&Registry&&Registry->Contains(P)&&Reachable(*C,*P,250.))
        {X.Skill.bAtResetService=true;break;}
    const FName Shop=C->ActiveShop();
    if(!Shop.IsNone()&&C->TradeSession.Target.IsValid()&&C->TradeSession.Target.Get()==Target&&!Combat&&!Threatened)
    {X.bTradeSessionValid=true;X.ShopId=Shop.ToString();X.TradeTargetStableId=Target->Spec.Id.ToString();}
    if(Command.Type==EAetherCommandType::DropItem)
    {
        // ID、落地点和区域都由服务器决定；请求不提供任意新容器位置。
        FHitResult Hit;const FVector Start=C->GetActorLocation()+C->GetActorForwardVector()*90;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(NativeDrop),false,C);
        const bool Ground=GetWorld()->LineTraceSingleByChannel(Hit,Start,Start-FVector(0,0,300),ECC_Visibility,Q)&&Hit.ImpactNormal.Z>.6;
        auto& A=X.Container;A.bAuthorized=true;A.bTargetReady=true;A.bValidDropLocation=Ground;
        A.ContainerId=TEXT("Drop_")+Command.CommandId.ToString(EGuidFormats::Digits);
        A.DropLocation=Hit.ImpactPoint+FVector(0,0,25);
        A.RegionId=FString::Printf(TEXT("%d_%d"),FMath::FloorToInt(A.DropLocation.X/7000),FMath::FloorToInt(A.DropLocation.Y/7000));
    }
    else if(Command.Type==EAetherCommandType::PickUpItem||Command.Type==EAetherCommandType::TransferItem)
    {
        auto* Found=NativeContainers.Find(Command.TargetStableId);auto* A=Found?Found->Get():nullptr;
        if(IsValid(A)&&A->Allows(Profile.CharacterId)&&Reachable(*C,*A,250.))
        {
            auto& Access=X.Container;Access.ContainerId=A->StableId;Access.TargetStableId=A->StableId;
            Access.bAuthorized=true;Access.bTargetReady=true;Access.bCanWithdraw=true;Access.bSafeToStore=!Combat&&!Threatened;
            Access.bInventoryPickup=A->ContainerKind==uint8(EAetherContainerKind::WorldDrop);
            Access.bCanDeposit=!Access.bInventoryPickup&&!Combat&&!Threatened;
            // 箱子双栏的显式会话由打开入口登记，不能把仅路过可达的箱子当成已打开。
            Access.bContainerSession=NativeContainerSessions.FindRef(&PC)==A;
        }
    }
    return true;
}
bool AAetherFrontierMode::OpenNativeContainer(AAetherPlayerController* PC,const FString& Id)
{
    if(!PC||!bNativeSceneReady)return false;
    auto* C=Cast<AAetherFrontierCharacter>(PC->GetPawn());auto* Found=NativeContainers.Find(Id);auto* A=Found?Found->Get():nullptr;
    if(!C||!C->ProfileState()||!C->Ready()||C->HasRecentCombat(8)||!IsValid(A)||
        A->ContainerKind==uint8(EAetherContainerKind::WorldDrop)||!A->Allows(C->ProfileState()->Profile.CharacterId)||!Reachable(*C,*A,250.))return false;
    NativeContainerSessions.Add(PC,A);return true;
}
