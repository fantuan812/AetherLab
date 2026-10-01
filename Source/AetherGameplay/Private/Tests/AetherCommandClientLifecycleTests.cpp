#include "Misc/AutomationTest.h"
#include "Networking/AetherCommandClient.h"
#include "Framework/AetherPlayerController.h"
#include "Definitions/AetherV10Definitions.h"
#include "Persistence/AetherWorldBootstrap.h"
#include "Profile/AetherProfileCodec.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/Crc.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherCommandClientLifecycleTest,"Aether.V10.Network.CurrentControllerOwnsCommandClient",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherCommandClientLifecycleTest::RunTest(const FString&)
{
    FString Why;const auto& D=FAetherV10Definitions::Get();
    if(!TestTrue(TEXT("Definitions ready"),D.bValid))return false;
    auto* GI=NewObject<UGameInstance>(GEngine);GI->AddToRoot();GI->InitializeStandalone(NAME_None,nullptr);
    auto* World=GI->GetWorld();UWorld* Foreign=nullptr;
    ON_SCOPE_EXIT
    {
        if(Foreign)Foreign->DestroyWorld(false);
        GI->Shutdown();if(World){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}GI->RemoveFromRoot();
    };
    if(!TestNotNull(TEXT("Isolated local world"),World))return false;
    auto* Player=GI->CreateLocalPlayer(0,Why,false);
    if(!TestNotNull(TEXT("Actual local player"),Player))return false;
    auto* Client=Player->GetSubsystem<UAetherCommandClient>();
    if(!TestNotNull(TEXT("Production command client"),Client))return false;
    auto* First=World->SpawnActor<AAetherPlayerController>();auto* Second=World->SpawnActor<AAetherPlayerController>();
    if(!TestNotNull(TEXT("Initial controller"),First)||!TestNotNull(TEXT("Replacement controller"),Second))return false;
    const FString Identity=TEXT("CommandOwnerFixture");const FGuid Realm=FGuid::NewGuid(),FirstChannel=FGuid::NewGuid();
    FAetherStoredAggregate Initial;
    if(!TestTrue(TEXT("Build native profile"),AetherProfileBootstrap::BuildNew(Identity,Initial,Why)))return false;
    FAetherProfileStateV10 VersionOne;
    if(!TestTrue(TEXT("Decode native profile"),AetherProfileCodec::Decode(Initial.Payload,D.Items,D.Skills,D.Rules,VersionOne,Why)))return false;
    VersionOne.Revision=1;TArray<uint8> Updated;
    if(!TestTrue(TEXT("Encode later confirmed snapshot"),AetherProfileCodec::Encode(VersionOne,D.Items,D.Skills,D.Rules,Updated,Why)))return false;
    const auto Deliver=[&](AAetherPlayerController* PC,FGuid Channel,const TArray<uint8>& Bytes,int64 Revision)
    {
        const FGuid Transfer=FGuid::NewGuid();
        for(int32 Offset=0;Offset<Bytes.Num();Offset+=AetherV10Network::ChunkBytes)
        {
            FAetherV10SnapshotChunk Chunk;Chunk.Channel=Channel;Chunk.Transfer=Transfer;Chunk.Revision=Revision;
            Chunk.Total=Bytes.Num();Chunk.Offset=Offset;Chunk.Checksum=FCrc::MemCrc32(Bytes.GetData(),Bytes.Num());
            Chunk.Bytes.Append(Bytes.GetData()+Offset,FMath::Min(AetherV10Network::ChunkBytes,Bytes.Num()-Offset));
            Client->ReceiveChunk(PC,Chunk);
        }
    };
    First->SetPlayer(Player);Client->ReceiveChannel(First,FirstChannel,Identity,Realm);Deliver(First,FirstChannel,Initial.Payload,0);
    if(!TestTrue(TEXT("First accepted transport publishes decoded profile"),Client->GetProfile().IsSet()&&Client->GetChannel()==FirstChannel))return false;
    TestTrue(TEXT("Idle ready channel still observes controller changes"),Client->IsTickable()&&!Client->HasPending());
    FAetherPlayerCommand Command;Command.ProtocolVersion=AetherCommands::LatestProtocolVersion;
    const FGuid Nonce=FGuid::NewGuid();Command.CommandId=FGuid(0,1,Nonce.C,Nonce.D);
    Command.Type=EAetherCommandType::SortInventory;Command.ExpectedProfileRevision=0;TArray<uint8> Frozen;
    if(!TestTrue(TEXT("Encode immutable request"),AetherCommands::Encode(Command,Frozen,Why))||
        !TestTrue(TEXT("Submit initial request"),Client->Submit(FirstChannel,Identity,Frozen,Why)))return false;
    if(!TestEqual(TEXT("Exactly one original pending request"),Client->Pending.Num(),1))return false;
    const auto Original=Client->Pending[0];
    FAetherCommandResult Receipt;Receipt.CommandId=Command.CommandId;Receipt.Code=EAetherCommandCode::Applied;Receipt.FinalProfileRevision=1;
    FAetherV10ReplyPacket Reply;Reply.Channel=FirstChannel;
    if(!TestTrue(TEXT("Encode confirmed receipt"),AetherCommands::EncodeResult(Receipt,Reply.Bytes,Why)))return false;

    Second->SetPlayer(Player);
    TestTrue(TEXT("Replacement precedes old controller destruction"),IsValid(First)&&Player->GetPlayerController(World)==Second);
    TestFalse(TEXT("Still-live old controller is no longer transport owner"),Client->AcceptsControllerIdentity(First));
    TestTrue(TEXT("Current controller identity is separate from channel acceptance"),Client->AcceptsControllerIdentity(Second)&&!Client->Matches(Second,FirstChannel));
    TestTrue(TEXT("Stale presentation is hidden synchronously before tick"),Client->PresentationState()==EAetherCommandPresentation::Closed&&
        !Client->GetChannel().IsValid()&&!Client->GetProfile().IsSet()&&Client->GetOwnerIdentity().IsEmpty());
    TestFalse(TEXT("Submit cannot run through old live controller"),Client->Submit(FirstChannel,Identity,Frozen,Why));
    TestFalse(TEXT("Retry cannot authorize old live controller"),Client->RetryPending());
    Client->ReceiveReply(First,Reply);Deliver(First,FirstChannel,Updated,1);
    if(!TestTrue(TEXT("Late old reply and snapshot cannot mutate accepted request"),Client->Pending.Num()==1&&
        !Client->Pending[0].Receipt.IsSet()&&Client->Profile.IsSet()&&Client->Profile->Revision==0))return false;
    Client->Pending[0].NextAttempt=0;const int32 BeforeAttempts=Client->Pending[0].Attempts;
    int32 Changes=0;const auto Handle=Client->OnChanged.AddLambda([&]{++Changes;});
    Client->Tick(0);Client->Tick(0);Client->OnChanged.Remove(Handle);
    TestEqual(TEXT("Controller invalidation notifies exactly once"),Changes,1);
    TestTrue(TEXT("Invalidation clears view without retransmitting or losing request"),Client->Controller.IsExplicitlyNull()&&!Client->Channel.IsValid()&&
        !Client->Profile.IsSet()&&Client->Pending.Num()==1&&Client->Pending[0].Attempts==BeforeAttempts&&
        Client->Pending[0].Id==Original.Id&&Client->Pending[0].Realm==Original.Realm&&Client->Pending[0].Owner==Original.Owner&&Client->Pending[0].Bytes==Original.Bytes);
    Client->ReceiveChannel(First,FGuid::NewGuid(),Identity,Realm);
    TestFalse(TEXT("Old controller cannot reinstall its channel"),Client->GetChannel().IsValid());

    const FGuid OtherChannel=FGuid::NewGuid();const FString OtherIdentity=TEXT("OtherOwnerFixture");FAetherStoredAggregate Other;
    if(!TestTrue(TEXT("Build other owner profile"),AetherProfileBootstrap::BuildNew(OtherIdentity,Other,Why)))return false;
    Client->ReceiveChannel(Second,OtherChannel,OtherIdentity,Realm);Deliver(Second,OtherChannel,Other.Payload,0);
    Client->Tick(0);
    TestTrue(TEXT("Other owner neither inherits nor erases original pending request"),Client->Pending.Num()==1&&!Client->HasPending()&&
        !Client->RetryPending()&&Client->Pending[0].Bytes==Frozen&&Client->Pending[0].Attempts==BeforeAttempts);
    const FGuid OtherRealmChannel=FGuid::NewGuid();
    Client->ReceiveChannel(Second,OtherRealmChannel,Identity,FGuid::NewGuid());Deliver(Second,OtherRealmChannel,Initial.Payload,0);
    TestTrue(TEXT("Same owner in another realm cannot authorize original request"),!Client->HasPending()&&!Client->RetryPending()&&Client->Pending.Num()==1);
    const FGuid CurrentChannel=FGuid::NewGuid();
    Client->ReceiveChannel(Second,CurrentChannel,Identity,Realm);Deliver(Second,CurrentChannel,Initial.Payload,0);Client->Tick(0);
    TestTrue(TEXT("Returning to exact identity waits for explicit reauthorization"),Client->HasPending()&&
        Client->PresentationState()==EAetherCommandPresentation::Recovering&&Client->Pending[0].AuthorizedChannel==FirstChannel&&Client->Pending[0].Attempts==BeforeAttempts);
    TestTrue(TEXT("Explicit retry reuses the original command on current channel"),Client->RetryPending()&&
        Client->Pending[0].AuthorizedChannel==CurrentChannel&&Client->Pending[0].Id==Command.CommandId&&Client->Pending[0].Bytes==Frozen);
    Reply.Channel=CurrentChannel;Client->ReceiveReply(Second,Reply);
    TestTrue(TEXT("Confirmed receipt still waits for its corresponding profile revision"),Client->Pending.Num()==1&&Client->Pending[0].Receipt.IsSet());
    Client->DetachController(Second);
    TestTrue(TEXT("Detaching cannot discard an accepted durable result"),Client->Pending.Num()==1&&Client->Pending[0].Receipt->FinalProfileRevision==1);
    const FGuid RecoveryChannel=FGuid::NewGuid();
    Client->ReceiveChannel(Second,RecoveryChannel,Identity,Realm);Deliver(Second,RecoveryChannel,Updated,1);
    TestTrue(TEXT("A later matching snapshot retires receipt exactly once without resend"),Client->Pending.IsEmpty()&&Client->GetProfile().IsSet()&&Client->GetProfile()->Revision==1);

    Foreign=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Different world for same GI boundary"),Foreign))return false;
    Foreign->SetGameInstance(GI);auto* ForeignPC=Foreign->SpawnActor<AAetherPlayerController>();
    if(!TestNotNull(TEXT("Foreign-world controller"),ForeignPC))return false;
    ForeignPC->SetPlayer(Player);
    TestFalse(TEXT("A local current PC from a noncurrent GI world is rejected"),Client->AcceptsControllerIdentity(ForeignPC));
    Client->ReceiveChannel(ForeignPC,FGuid::NewGuid(),Identity,Realm);
    TestFalse(TEXT("Foreign world cannot expose cached current-world profile"),Client->GetProfile().IsSet());
    Second->SetPlayer(Player);
    TWeakObjectPtr<AAetherPlayerController> Ended=Second;
    TestTrue(TEXT("Destroy actual current controller"),Second->Destroy());Player->PlayerController=nullptr;
    TestTrue(TEXT("Destroyed controller leaves a real stale weak reference"),Ended.IsStale(true));
    // 某些无BeginPlay夹具会省略EndPlay通知；直接保留弱身份验证Tick兜底，不伪造成功回执。
    Client->Controller=Ended;Client->Channel=RecoveryChannel;
    TestTrue(TEXT("Stale controller cleanup remains tickable without pending commands"),Client->IsTickable());
    TestTrue(TEXT("No-PC gap immediately hides Ready"),Client->PresentationState()==EAetherCommandPresentation::Closed);
    Client->Tick(0);
    TestTrue(TEXT("Stale cleanup sleeps after one invalidation"),Client->Controller.IsExplicitlyNull()&&!Client->IsTickable()&&!Client->GetProfile().IsSet());
    return true;
}
#endif
