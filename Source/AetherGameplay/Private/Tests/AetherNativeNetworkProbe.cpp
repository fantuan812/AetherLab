#include "Tests/AetherNativeNetworkProbe.h"
#include "Framework/AetherPlayerController.h"
#include "Framework/AetherProgression.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Networking/AetherCommandClient.h"
#include "Contracts/AetherTransaction.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

void AetherNativeNetworkProbe::Tick(AAetherPlayerController* PC)
{
#if !UE_BUILD_SHIPPING
    if(!FParse::Param(FCommandLine::Get(),TEXT("AetherV10NativeNetwork"))||!PC||!PC->IsLocalController()||!PC->GetLocalPlayer())return;
    struct FProbe {int32 Stage=0;double Start=FPlatformTime::Seconds(),Next=0;FGuid Item;int64 Revision=-1;TArray<uint8> Move;bool Done=false,Listening=false;};
    static FProbe S;if(S.Done)return;
    auto* Client=PC->GetLocalPlayer()->GetSubsystem<UAetherCommandClient>();
    if(!S.Listening){S.Listening=true;Client->OnResult.AddWeakLambda(PC,[](const FAetherCommandResult& R){UE_LOG(LogTemp,Display,TEXT("V10_NATIVE_RECEIPT code=%d revision=%lld"),int32(R.Code),R.FinalProfileRevision);});}
    const double Now=FPlatformTime::Seconds();
    const auto Fail=[&](const FString& Why){S.Done=true;UE_LOG(LogTemp,Error,TEXT("V10_NATIVE_NETWORK_FAIL stage=%d %s"),S.Stage,*Why);FPlatformMisc::RequestExitWithStatus(false,1);};
    if(Now-S.Start>100){Fail(TEXT("Native snapshot/command deadline"));return;}
    auto* Pawn=Cast<AAetherFrontierCharacter>(PC->GetPawn());
    // 初始快照到达不代表目的地传送已经提交，遵循真实交互入口的状态门禁。
    if(!Pawn||!Pawn->Ready()||Pawn->bTravelPending||!Client->GetProfile().IsSet()||!Client->GetChannel().IsValid()||Now<S.Next)return;
    const auto& P=Client->GetProfile().GetValue();
    auto* OwnState=PC->GetPlayerState<AAetherPlayerState>();
    // 可靠快照 RPC 可以早于 PlayerState 属性到达；尚不知道自身 PS 时不能把自身认作他人。
    if(!OwnState||OwnState!=Pawn->ProfileState()||!OwnState->Profile.CharacterId.Equals(P.CharacterId,ESearchCase::CaseSensitive))return;
    if(!P.CharacterId.Equals(Client->GetOwnerIdentity(),ESearchCase::CaseSensitive)){Fail(TEXT("Snapshot owner mismatch"));return;}
    // 普通客户端应只看到其他人的公开队伍/称呼，不能复制其库存、技能或原生 DTO。
    int32 Players=0;
    for(TActorIterator<AAetherPlayerState> It(PC->GetWorld());It;++It)
    {
        if(!It->DisplayName.IsEmpty())++Players;
        if(!PC->HasAuthority()&&*It!=PC->PlayerState&&
            (!It->Profile.Inventory.IsEmpty()||!It->Profile.CharacterId.IsEmpty()||It->GetNativeProfile()||!It->SkillGrants.Rows.IsEmpty()))
        {Fail(FString::Printf(TEXT("Other player private profile replicated self=%s other=%s public=%s private=%s items=%d grants=%d dto=%d"),*GetNameSafe(PC->PlayerState),*It->GetName(),*It->DisplayName,*It->Profile.CharacterId,It->Profile.Inventory.Num(),It->SkillGrants.Rows.Num(),It->GetNativeProfile()!=nullptr));return;}
    }
    const auto Send=[&](EAetherCommandType Type)
    {
        FAetherPlayerCommand C;C.ProtocolVersion=AetherCommands::LatestProtocolVersion;C.Type=Type;
        C.ExpectedProfileRevision=P.Revision;C.CommandId=AetherTransactions::NewCommandId(P.Revision);C.ItemInstanceId=S.Item;
        if(Type==EAetherCommandType::SetItemFavorite)C.Enabled=true;else C.DestinationIndex=31;
        FString Why;TArray<uint8> Bytes;
        if(!AetherCommands::Encode(C,Bytes,Why)||!Client->Submit(Client->GetChannel(),Client->GetOwnerIdentity(),Bytes,Why)){Fail(Why);return false;}
        if(Type==EAetherCommandType::MoveItem)S.Move=MoveTemp(Bytes);return true;
    };
    const bool Recovery=FParse::Param(FCommandLine::Get(),TEXT("AetherNativeRecovery"));
    if(S.Stage==0)
    {
#if DO_ENABLE_NET_TEST
        int32 Lag=0,Loss=0;FParse::Value(FCommandLine::Get(),TEXT("PktLag="),Lag);FParse::Value(FCommandLine::Get(),TEXT("PktLoss="),Loss);
        if(auto* Driver=PC->GetWorld()->GetNetDriver())
        {
            const auto& Net=Driver->PacketSimulationSettings;
            if(Net.PktLag!=Lag||Net.PktLoss!=Loss){Fail(TEXT("Requested network impairment was not installed"));return;}
            UE_LOG(LogTemp,Display,TEXT("V10_NATIVE_NET_EMULATION lag=%d loss=%d"),Net.PktLag,Net.PktLoss);
        }
#endif
        if(P.Inventory.Items.IsEmpty()){Fail(TEXT("Seed inventory absent"));return;}
        if(Recovery)
        {
            const auto* Item=P.Inventory.At(31);
            if(!Item||!Item->bFavorite||Item->Quantity!=2){Fail(TEXT("Durable inventory did not survive reconnect/restart"));return;}
            S.Item=Item->InstanceId;S.Revision=P.Revision;S.Stage=4;
        }
        else
        {
            S.Item=P.Inventory.Items[0].InstanceId;
            if(Send(EAetherCommandType::SetItemFavorite))S.Stage=1;
        }
        S.Next=Now+.2;return;
    }
    const auto* Item=P.Inventory.Find(S.Item);
    if(!Item){Fail(TEXT("Stable item identity lost"));return;}
    if(S.Stage==1)
    {
        if(Client->HasPending()||!Item->bFavorite)return;
        if(Send(EAetherCommandType::MoveItem))S.Stage=2;
        return;
    }
    if(S.Stage==2)
    {
        if(Client->HasPending()||Item->SlotIndex!=31)return;
        S.Revision=P.Revision;
        // 已提交字节经同一 RPC 再送，确认丢回执重试不会重复转移或增加版本。
        FAetherV10CommandPacket Packet;Packet.Channel=Client->GetChannel();Packet.Bytes=S.Move;PC->ServerV10Command(Packet);
        S.Stage=3;S.Next=Now+1;return;
    }
    if(S.Stage==3){Client->RequestSnapshot();S.Stage=4;S.Next=Now+1.5;return;}
    int32 Expected=4;FParse::Value(FCommandLine::Get(),TEXT("AetherProbePlayers="),Expected);
    if(Players<Expected)return;
    if(Client->HasPending()||P.Revision!=S.Revision||Item->SlotIndex!=31||!Item->bFavorite||Item->Quantity!=2)
    {Fail(TEXT("Replay changed durable inventory or left pending work"));return;}
    UE_LOG(LogTemp,Display,TEXT("V10_NATIVE_NETWORK_PASS owner=%s players=%d recovery=%d revision=%lld net=%d"),*P.CharacterId,Players,Recovery,P.Revision,int32(PC->GetNetMode()));
    S.Done=true;
#endif
}
