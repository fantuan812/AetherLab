#include "AetherServices.h"
#include "AetherGuide.h"
#include "AetherFrontier.h"
#include "ReactiveWorldSubsystem.h"

bool AetherServices::IsService(FName Service)
{return Service=="Source"||Service=="SupplyRestored"||Service=="Receiver";}
FString AetherServices::Message(EAetherServiceResult Result)
{
    switch(Result)
    {
    case EAetherServiceResult::Committed:return TEXT("操作与角色进度已共同保存。");
    case EAetherServiceResult::AlreadyProcessed:return TEXT("该操作已保存，无需重复执行。");
    case EAetherServiceResult::Unauthorized:return TEXT("当前角色无权操作。");
    case EAetherServiceResult::TargetChanged:return TEXT("目标已变化、被遮挡或超出交互范围。");
    case EAetherServiceResult::StaleProfile:return TEXT("角色进度已变化，请重新交互。");
    case EAetherServiceResult::InsufficientPower:return TEXT("接收端电力不足；连接金属杆或使用机械泵。");
    case EAetherServiceResult::Busy:return TEXT("反应正在结算，请稍后重试同一操作。");
    case EAetherServiceResult::StorageFailure:return TEXT("保存失败，供水、电源与奖励均未提交；可重试。");
    default:return TEXT("无效的服务操作。");
    }
}

EAetherServiceResult AAetherFrontierMode::ExecuteWorldService(AAetherFrontierCharacter* C,const FAetherWorldServiceCommand& Command,const FAetherInteractionTarget* Pinned)
{
    auto* PS=IsValid(C)?C->ProfileState():nullptr;
    auto* State=GetGameState<AAetherFrontierState>();
    if(!HasAuthority()||!PS||!PS->HasAuthority()||C->GetWorld()!=GetWorld()||!C->Alive()||!State)
        return EAetherServiceResult::Unauthorized;
    if(!Command.Id.IsValid()||Command.TargetId.IsNone()||Command.ExpectedRevision<0||Command.ExpectedRevision==MAX_int32)
        return EAetherServiceResult::InvalidCommand;
    // Acknowledging an old receipt does not touch world state, even if the actor moved away.
    if(const auto* Receipt=Database->ServiceReceipts.FindByPredicate([&](const auto& R){return R.Command.Id==Command.Id;}))
        return Receipt->CharacterId==PS->Profile.CharacterId&&Receipt->Command.Matches(Command)
            ?EAetherServiceResult::AlreadyProcessed:EAetherServiceResult::InvalidCommand;
    if(C->bTravelPending)return EAetherServiceResult::Busy;
    if(PS->Profile.Revision!=Command.ExpectedRevision)return EAetherServiceResult::StaleProfile;
    if(const auto* Stored=Database->Profiles.FindByPredicate([&](const auto& P){return P.CharacterId==PS->Profile.CharacterId;}))
        if(Stored->Revision!=Command.ExpectedRevision)return EAetherServiceResult::StaleProfile;
    // 回执先于场景复验：对象已卸载时仍可确认旧提交，但新操作只能作用于原选择。
    const auto Selection=Pinned?*Pinned:AetherGuide::QueryTarget(C,Prop(Command.TargetId));
    auto* Target=Selection.Prop.Get();
    if(!AetherGuide::ValidateSelection(C,Selection)||Selection.Rescue.IsValid()||!Target||
        !Target->Spec.Id.ToString().Equals(Command.TargetId.ToString(),ESearchCase::CaseSensitive)||!AetherServices::IsService(Target->Service))
        return EAetherServiceResult::TargetChanged;
    if(Target->Service=="Receiver"&&!(Target->bWorkshopService?State->bWorkshopRestored:State->bSupplyRestored)&&Target->ReceivedPower<1)
        return EAetherServiceResult::InsufficientPower;

    auto* Candidate=DuplicateObject<UAetherFrontierSave>(Database,this);
    if(!CaptureWorldCandidate(Candidate))return EAetherServiceResult::Busy;
    auto Profile=PS->Profile;
    Profile.RefreshDaily(FDateTime::UtcNow().ToString(TEXT("%Y%m%d")));
    const bool NewPower=!Target->Mechanism->bPowerEnabled;
    if(Target->Service=="Source")
    {
        if(Target->bGlobalPowerService)Candidate->bPowerOn=NewPower;
        auto* Record=Candidate->World.FindByPredicate([&](const auto& R){return R.StableId==Target->Reactive->StableId;});
        if(!Record||!Record->bHasMechanism)return EAetherServiceResult::InvalidCommand;
        Record->bSourceEnabled=NewPower;
    }
    else if(Target->bWorkshopService)Candidate->bWorkshopRestored=true;
    else {Candidate->bSupplyRestored=true;Candidate->WorldFacts.Record("SupplyRestored",Target->Spec.Id);Profile.Observe("SupplyRestored");}
    AetherQuests::Settle(Profile,Candidate->WorldFacts,false);
    ++Profile.Revision;
    if(!Profile.Validate())return EAetherServiceResult::InvalidCommand;
    if(auto* Stored=Candidate->Profiles.FindByPredicate([&](const auto& P){return P.CharacterId==Profile.CharacterId;}))*Stored=Profile;
    else Candidate->Profiles.Add(Profile);
    FAetherWorldServiceReceipt Receipt;Receipt.Command=Command;Receipt.CharacterId=Profile.CharacterId;
    // Bounded persisted acknowledgement cache. Evicted commands have an older profile revision
    // and are rejected above, so eviction can never turn a retry into a new toggle/reward.
    if(Candidate->ServiceReceipts.Num()>=64)Candidate->ServiceReceipts.RemoveAt(0);
    Candidate->ServiceReceipts.Add(Receipt);
    if(!WriteDatabase(Candidate))return EAetherServiceResult::StorageFailure;

    PS->Profile=MoveTemp(Profile);PS->ForceNetUpdate();PS->OnProfilePublished.Broadcast();
    State->bSupplyRestored=Candidate->bSupplyRestored;State->bPowerOn=Candidate->bPowerOn;State->ForceNetUpdate();
    State->bWorkshopRestored=Candidate->bWorkshopRestored;
    if(Target->Service=="Source"){Target->Mechanism->bPowerEnabled=NewPower;Target->ForceNetUpdate();}
    return EAetherServiceResult::Committed;
}

void AAetherFrontierCharacter::RefreshInteractionFocus()
{
    AActor* Previous=InteractionFocus.Prop.IsValid()?static_cast<AActor*>(InteractionFocus.Prop.Get()):static_cast<AActor*>(InteractionFocus.Rescue.Get());
    InteractionFocus=AetherGuide::SelectInteraction(this,Previous);bHasInteractionFocus=true;
}
void AAetherFrontierCharacter::InteractV4()
{
    if(bPanel)return;
    auto* PS=ProfileState();if(!PS)return;
    if(!bHasInteractionFocus)RefreshInteractionFocus();
    // 输入使用最近一次真正显示的快照；失效时只刷新，不在同一次按键偷偷执行新目标。
    const auto Selection=InteractionFocus;
    if(!AetherGuide::ValidateSelection(this,Selection)){RefreshInteractionFocus();return;}
    auto* Target=Selection.Prop.Get();
    if(!Target||!AetherServices::IsService(Selection.ActionId))
    {
        PendingService.Id.Invalidate();
        ServerInteractTarget(Target?static_cast<AActor*>(Target):static_cast<AActor*>(Selection.Rescue.Get()),Selection.StableId,Selection.ActionId,Selection.ProfileRevision);
        return;
    }
    if(PS->Profile.Revision<MinimumServiceRevision)return;
    if(!PendingService.Id.IsValid()||PendingService.TargetId!=Selection.StableId||PendingService.ExpectedRevision!=Selection.ProfileRevision)
    {PendingService.Id=FGuid::NewGuid();PendingService.TargetId=Selection.StableId;PendingService.ExpectedRevision=Selection.ProfileRevision;}
    ServerWorldService(PendingService,Target,Selection.ActionId);
}
void AAetherFrontierCharacter::ServerInteractTarget_Implementation(AActor* Target,FName StableId,FName ActionId,int32 ExpectedProfileRevision)
{
    auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();if(!Mode||!ProfileState())return;
    if(CombatTime()<NextServerAction)return;NextServerAction=CombatTime()+.12f;
    FAetherInteractionTarget Selection;Selection.Prop=Cast<AAetherFrontierProp>(Target);
    Selection.Rescue=Cast<AAetherFrontierCharacter>(Target);Selection.StableId=StableId;Selection.ActionId=ActionId;Selection.ProfileRevision=ExpectedProfileRevision;
    // 有持久回执的世界服务只允许专用入口，不能经普通交互 RPC 绕过重试身份。
    if(AetherServices::IsService(ActionId)){Notify(TEXT("请使用可重试的世界服务操作。"));return;}
    Notify(Mode->InteractTarget(this,Selection));
}
void AAetherFrontierCharacter::ServerWorldService_Implementation(FAetherWorldServiceCommand Command,AAetherFrontierProp* Target,FName ActionId)
{
    auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();auto* PS=ProfileState();if(!Mode||!PS)return;
    if(CombatTime()<NextServerAction)return;NextServerAction=CombatTime()+.12f;
    FAetherInteractionTarget Selection;Selection.Prop=Target;Selection.StableId=Command.TargetId;
    Selection.ActionId=ActionId;Selection.ProfileRevision=Command.ExpectedRevision;
    const auto Result=Mode->ExecuteWorldService(this,Command,&Selection);
    WorldServiceResult(Command.Id,Result,PS->Profile.Revision);
}
void AAetherFrontierCharacter::WorldServiceResult_Implementation(FGuid Id,EAetherServiceResult Result,int32 Revision)
{
    MinimumServiceRevision=FMath::Max(MinimumServiceRevision,Revision);
    if(PendingService.Id==Id&&Result!=EAetherServiceResult::StorageFailure&&Result!=EAetherServiceResult::Busy)PendingService.Id.Invalidate();
    Notify(AetherServices::Message(Result));
}
