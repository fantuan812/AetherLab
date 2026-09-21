#include "Framework/AetherFrontier.h"
#include "ReactiveWorldSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/MemoryWriter.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
TArray<uint8> ProfileBytes(FAetherProfile P)
{
    TArray<uint8> Bytes;FMemoryWriter Ar(Bytes);FAetherProfile::StaticStruct()->SerializeBin(Ar,&P);return Bytes;
}
FAetherWorldServiceCommand TestCommand(FName Target,int32 Revision,int32 Serial)
{
    FAetherWorldServiceCommand R;R.Id=FGuid(0xA375E8,0,0,Serial);R.TargetId=Target;R.ExpectedRevision=Revision;return R;
}
}
void AAetherFrontierMode::CheckServices()
{
    if(bLightCheckStarted)return;
    auto* C=Cast<AAetherFrontierCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
    TArray<FReactiveSaveRecord> Ready;
    if(!C||!C->ProfileState()||!GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->Capture(Ready))
    {if(Elapsed>8){UE_LOG(LogTemp,Error,TEXT("AETHER_SERVICE_FAIL timeout"));FPlatformMisc::RequestExitWithStatus(false,1);}return;}
    bLightCheckStarted=true;
    auto* PS=C->ProfileState();auto* State=GetGameState<AAetherFrontierState>();auto* Source=Prop("PowerSource")->Mechanism.Get();
    FString Phase;FParse::Value(FCommandLine::Get(),TEXT("AetherServicePhase="),Phase);
    auto Check=[&](bool Pass,const TCHAR* Id){UE_LOG(LogTemp,Display,TEXT("SERVICE_CHECK %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),Id);if(!Pass)++Failures;};
    auto MoveTo=[&](FName Id){C->SetActorLocation(Prop(Id)->GetActorLocation()+FVector(0,-160,100));};
    auto UnchangedOnFailure=[&](const FAetherWorldServiceCommand& Command,EAetherServiceResult Expected,const TCHAR* Id)
    {
        const auto Before=ProfileBytes(PS->Profile);const auto* BeforeDB=Database.Get();
        const int32 Generation=Database->Generation;const int32 Receipts=Database->ServiceReceipts.Num();
        const bool Supply=State->bSupplyRestored,Power=State->bPowerOn,Enabled=Source->bPowerEnabled;
        const double Energy=Source->RemainingEnergyJ,Age=Source->SourceAge;
        const auto Result=ExecuteWorldService(C,Command);
        Check(Result==Expected&&Before==ProfileBytes(PS->Profile)&&Database==BeforeDB&&Database->Generation==Generation
            &&Database->ServiceReceipts.Num()==Receipts&&State->bSupplyRestored==Supply&&State->bPowerOn==Power
            &&Source->bPowerEnabled==Enabled&&Source->RemainingEnergyJ==Energy&&Source->SourceAge==Age,Id);
    };
    const auto SupplyCommand=TestCommand("Pump",1,1);
    const auto PowerCommand=TestCommand("PowerSource",2,2);
    if(Phase=="Fail")
    {
        auto Fixture=PS->Profile;Fixture.Claims={FName("Q_Main_01"),FName("Q_Main_02"),FName("Q_Main_03")};Fixture.bRegistered=true;
        Check(Database->Generation==0&&Commit(PS,Fixture)&&SaveWorld(),TEXT("SETUP persisted isolated profile and world"));
        Check(PS->Profile.Revision==1&&Database->Generation==2&&!State->bSupplyRestored&&State->bPowerOn,TEXT("SETUP expected initial state"));
        MoveTo("PowerReceiver");Prop("PowerReceiver")->ReceivedPower=0;
        UnchangedOnFailure(TestCommand("PowerReceiver",1,3),EAetherServiceResult::InsufficientPower,TEXT("GUARD unpowered receiver rejected"));
        MoveTo("Pump");bFailWrites=true;
        UnchangedOnFailure(SupplyCommand,EAetherServiceResult::StorageFailure,TEXT("AUD8-01 supply failed write changes nothing"));
        MoveTo("PowerSource");
        UnchangedOnFailure(TestCommand("PowerSource",1,4),EAetherServiceResult::StorageFailure,TEXT("AUD8-02 source failed write changes nothing"));
        bFailWrites=false;MoveTo("Pump");bFailAfterDataWrite=true;
        UnchangedOnFailure(SupplyCommand,EAetherServiceResult::StorageFailure,TEXT("AUD8-01 inactive data without commit marker changes nothing"));
        bFailAfterDataWrite=false;
    }
    else if(Phase=="Retry")
    {
        Check(Database->Generation==2&&PS->Profile.Revision==1&&!State->bSupplyRestored&&State->bPowerOn&&Source->bPowerEnabled
            &&PS->Profile.Gold==0&&PS->Profile.Count("TideStaff")==0&&!PS->Profile.Evidence.Contains("SupplyRestored")&&Database->ServiceReceipts.IsEmpty(),
            TEXT("AUD8-01 restart ignores failed candidate generation"));
        MoveTo("Pump");const int32 Gold=PS->Profile.Gold,Experience=PS->Profile.Experience;
        Check(ExecuteWorldService(C,SupplyCommand)==EAetherServiceResult::Committed&&State->bSupplyRestored&&Database->bSupplyRestored
            &&PS->Profile.Gold==Gold+60&&PS->Profile.Experience==Experience+100&&PS->Profile.Count("TideStaff")==1
            &&PS->Profile.Claims.Contains(FName("Q_Main_05"))&&PS->Profile.Revision==2&&Database->Generation==3,
            TEXT("AUD8-03 supply retry commits world profile reward and receipt together"));
        UnchangedOnFailure(SupplyCommand,EAetherServiceResult::AlreadyProcessed,TEXT("AUD8-03 duplicate supply receipt has no second reward"));
        auto Conflict=SupplyCommand;Conflict.TargetId="PowerSource";
        UnchangedOnFailure(Conflict,EAetherServiceResult::InvalidCommand,TEXT("GUARD reused command id cannot change payload"));
        const auto Receipts=Database->ServiceReceipts;Database->ServiceReceipts.Reset();
        UnchangedOnFailure(SupplyCommand,EAetherServiceResult::StaleProfile,TEXT("GUARD expired receipt rejected by old revision"));Database->ServiceReceipts=Receipts;
        UnchangedOnFailure(PowerCommand,EAetherServiceResult::TargetChanged,TEXT("GUARD exact target validation rejects remote target"));
        MoveTo("PowerSource");bFailWrites=true;
        UnchangedOnFailure(PowerCommand,EAetherServiceResult::StorageFailure,TEXT("AUD8-02 source retry still fails atomically"));
        bFailWrites=false;bFailAfterDataWrite=true;
        UnchangedOnFailure(PowerCommand,EAetherServiceResult::StorageFailure,TEXT("AUD8-02 incomplete source generation not published"));
        bFailAfterDataWrite=false;
        const double Energy=Source->RemainingEnergyJ,Age=Source->SourceAge;
        Check(ExecuteWorldService(C,PowerCommand)==EAetherServiceResult::Committed&&!State->bPowerOn&&!Database->bPowerOn&&!Source->bPowerEnabled
            &&Source->RemainingEnergyJ==Energy&&Source->SourceAge==Age&&PS->Profile.Revision==3&&Database->Generation==4,
            TEXT("AUD8-03 source retry publishes exactly one toggle"));
        const auto* SavedSource=Database->World.FindByPredicate([&](const auto& R){return R.StableId==Prop("PowerSource")->Reactive->StableId;});
        Check(SavedSource&&!SavedSource->bSourceEnabled&&SavedSource->RemainingEnergyJ==Energy&&SavedSource->SourceAge==Age,
            TEXT("AUD8-02 source checkpoint agrees with committed switch"));
        UnchangedOnFailure(PowerCommand,EAetherServiceResult::AlreadyProcessed,TEXT("AUD8-03 duplicate source does not toggle back"));
    }
    else if(Phase=="Reload")
    {
        Check(Database->Generation==4&&PS->Profile.Revision==3&&State->bSupplyRestored&&!State->bPowerOn&&!Source->bPowerEnabled
            &&PS->Profile.Gold==60&&PS->Profile.Experience==100&&PS->Profile.Count("TideStaff")==1&&Database->ServiceReceipts.Num()==2,
            TEXT("AUD8-03 restarted process restores committed world and one reward"));
        const auto* SavedSource=Database->World.FindByPredicate([&](const auto& R){return R.StableId==Prop("PowerSource")->Reactive->StableId;});
        Check(SavedSource&&SavedSource->RemainingEnergyJ==Source->RemainingEnergyJ&&SavedSource->SourceAge==Source->SourceAge,
            TEXT("AUD8-02 stopped source energy survives process restart"));
        UnchangedOnFailure(SupplyCommand,EAetherServiceResult::AlreadyProcessed,TEXT("AUD8-03 supply receipt survives process restart"));
        UnchangedOnFailure(PowerCommand,EAetherServiceResult::AlreadyProcessed,TEXT("AUD8-03 source receipt survives process restart"));
    }
    else Check(false,TEXT("SETUP unknown check phase"));
    UE_LOG(LogTemp,Display,TEXT("AETHER_SERVICE_%s phase=%s failures=%d"),Failures?TEXT("FAIL"):TEXT("PASS"),*Phase,Failures);
    FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherServiceLedger,"Aether.V8.ServiceReceiptCorruptionRejected",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherServiceLedger::RunTest(const FString&)
{
    auto* Save=NewObject<UAetherFrontierSave>();
    TestTrue(TEXT("Legacy save with absent receipts remains valid"),Save->ValidateWorldLedger());
    FAetherProfile P;P.CharacterId="ReceiptFixture";P.Revision=2;Save->Profiles.Add(P);
    FAetherWorldServiceReceipt R;R.Command=TestCommand("Pump",1,1);R.CharacterId=P.CharacterId;Save->ServiceReceipts.Add(R);
    TestTrue(TEXT("Receipt is backed by a newer stored profile"),Save->ValidateWorldLedger());
    Save->ServiceReceipts.Add(R);
    TestFalse(TEXT("Duplicate IDs cannot survive loading"),Save->ValidateWorldLedger());Save->ServiceReceipts.RemoveAt(1);
    Save->ServiceReceipts[0].CharacterId="OtherCharacter";
    TestFalse(TEXT("Orphan ownership rejected"),Save->ValidateWorldLedger());Save->ServiceReceipts[0]=R;
    Save->ServiceReceipts[0].Command.ExpectedRevision=2;
    TestFalse(TEXT("Uncommitted future receipt rejected"),Save->ValidateWorldLedger());Save->ServiceReceipts[0]=R;
    Save->ServiceReceipts[0].Command.TargetId=NAME_None;
    TestFalse(TEXT("Missing target rejected"),Save->ValidateWorldLedger());Save->ServiceReceipts[0]=R;
    Save->ServiceReceipts[0].Command.Id.Invalidate();
    TestFalse(TEXT("Invalid command identity rejected"),Save->ValidateWorldLedger());
    return true;
}
#endif
