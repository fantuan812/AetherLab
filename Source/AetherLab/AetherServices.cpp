#include "AetherServices.h"
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

EAetherServiceResult AAetherFrontierMode::ExecuteWorldService(AAetherFrontierCharacter* C,const FAetherWorldServiceCommand& Command)
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
    const auto Selection=AetherGuide::SelectInteraction(C);
    auto* Target=Selection.Prop.Get();
    if(Selection.Rescue.IsValid()||!Target||Target->Spec.Id!=Command.TargetId||!AetherServices::IsService(Target->Service))
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

    PS->Profile=MoveTemp(Profile);PS->ForceNetUpdate();
    State->bSupplyRestored=Candidate->bSupplyRestored;State->bPowerOn=Candidate->bPowerOn;State->ForceNetUpdate();
    State->bWorkshopRestored=Candidate->bWorkshopRestored;
    if(Target->Service=="Source"){Target->Mechanism->bPowerEnabled=NewPower;Target->ForceNetUpdate();}
    return EAetherServiceResult::Committed;
}

void AAetherFrontierCharacter::InteractV4()
{
    if(bPanel)return;
    auto* PS=ProfileState();const auto Selection=AetherGuide::SelectInteraction(this);
    auto* Target=Selection.Prop.Get();
    if(!PS||Selection.Rescue.IsValid()||!Target||!AetherServices::IsService(Target->Service))
    {PendingService.Id.Invalidate();ServerAction("Interact");return;}
    if(PS->Profile.Revision<MinimumServiceRevision)return; // wait for the acknowledged owner-only profile
    if(!PendingService.Id.IsValid()||PendingService.TargetId!=Target->Spec.Id||PendingService.ExpectedRevision!=PS->Profile.Revision)
    {PendingService.Id=FGuid::NewGuid();PendingService.TargetId=Target->Spec.Id;PendingService.ExpectedRevision=PS->Profile.Revision;}
    ServerWorldService(PendingService);
}
void AAetherFrontierCharacter::ServerWorldService_Implementation(FAetherWorldServiceCommand Command)
{
    auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();auto* PS=ProfileState();if(!Mode||!PS)return;
    if(CombatTime()<NextServerAction)return;NextServerAction=CombatTime()+.12f;
    const auto Result=Mode->ExecuteWorldService(this,Command);
    WorldServiceResult(Command.Id,Result,PS->Profile.Revision);
}
void AAetherFrontierCharacter::WorldServiceResult_Implementation(FGuid Id,EAetherServiceResult Result,int32 Revision)
{
    MinimumServiceRevision=FMath::Max(MinimumServiceRevision,Revision);
    if(PendingService.Id==Id&&Result!=EAetherServiceResult::StorageFailure&&Result!=EAetherServiceResult::Busy)PendingService.Id.Invalidate();
    Notify(AetherServices::Message(Result));
}
