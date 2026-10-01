#include "Framework/AetherPlayerController.h"
#include "Networking/AetherCommandRuntime.h"
#include "Networking/AetherCommandClient.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Framework/AetherFrontierMode.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Startup/AetherStartupClient.h"

void AAetherPlayerController::PublishStartupStatus(const FAetherStartupSnapshot& Snapshot)
{
    if(!HasAuthority()||!Snapshot.AttemptId.IsValid()||Snapshot.Sequence==0||
        (StartupAttempt==Snapshot.AttemptId&&(bStartupTerminal||StartupSequence>=Snapshot.Sequence)))return;
    StartupAttempt=Snapshot.AttemptId;StartupSequence=Snapshot.Sequence;
    bStartupTerminal=Snapshot.Stage==EAetherStartupStage::Failed||Snapshot.Stage==EAetherStartupStage::Cancelled;
    ClientV10StartupStatus(Snapshot);
}
void AAetherPlayerController::PublishStartupFailure(FAetherStartupSnapshot Snapshot,EAetherStartupFailure Failure)
{
    if(!HasAuthority()||!Snapshot.AttemptId.IsValid()||Failure==EAetherStartupFailure::None)return;
    if(StartupAttempt==Snapshot.AttemptId)Snapshot.Sequence=FMath::Max(Snapshot.Sequence,StartupSequence);
    if(Snapshot.Sequence==MAX_uint32)return;
    ++Snapshot.Sequence;Snapshot.Stage=EAetherStartupStage::Failed;Snapshot.FailureCode=Failure;
    PublishStartupStatus(Snapshot);
}
void AAetherPlayerController::ClientV10StartupStatus_Implementation(const FAetherStartupSnapshot& Snapshot)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherStartupClient>()->ReceiveStartup(this,Snapshot);}

void AAetherPlayerController::ServerV10Command_Implementation(const FAetherV10CommandPacket& P)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->Receive(this,P);}
void AAetherPlayerController::ServerV10RequestSnapshot_Implementation(FGuid Channel)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->RequestSnapshot(this,Channel);}
void AAetherPlayerController::ServerV10SnapshotAck_Implementation(FGuid Channel,FGuid Transfer,uint32 NextOffset)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->AcknowledgeSnapshot(this,Channel,Transfer,NextOffset);}
void AAetherPlayerController::ClientV10Channel_Implementation(FGuid Channel,const FString& CanonicalOwner,FGuid Realm)
{
    if(auto* LP=GetLocalPlayer())
    {
        auto* Commands=LP->GetSubsystem<UAetherCommandClient>();Commands->ReceiveChannel(this,Channel,CanonicalOwner,Realm);
        if(auto* GI=GetGameInstance())
            if(!Channel.IsValid()||(Realm.IsValid()&&Commands->GetChannel()==Channel&&Commands->GetOwnerIdentity().Equals(CanonicalOwner,ESearchCase::CaseSensitive)))
                GI->GetSubsystem<UAetherStartupClient>()->ObserveCommandChannel(this,Channel);
    }
}
void AAetherPlayerController::ClientV10Reply_Implementation(const FAetherV10ReplyPacket& P)
{if(auto* LP=GetLocalPlayer())LP->GetSubsystem<UAetherCommandClient>()->ReceiveReply(this,P);}
void AAetherPlayerController::ClientV10LootClaimResult_Implementation(FGuid Channel,FGuid OriginCommandId,FGuid LootInstanceId,EAetherLootClaimOutcome Outcome)
{
    auto* LP=GetLocalPlayer();auto* Client=LP?LP->GetSubsystem<UAetherCommandClient>():nullptr;
    if(!Client||!Channel.IsValid()||Channel!=Client->GetChannel()||!Client->AcceptsControllerIdentity(this)||
        !OriginCommandId.IsValid()||!LootInstanceId.IsValid())return;
    auto* C=Cast<AAetherFrontierCharacter>(GetPawn());if(!C)return;
    if(LootFeedbackChannel!=Channel){LootFeedbackChannel=Channel;CompletedLootFeedback.Reset();}
    const TPair<FGuid,FGuid> Key(OriginCommandId,LootInstanceId);if(CompletedLootFeedback.Contains(Key))return;
    FString Message;
    switch(Outcome)
    {
    case EAetherLootClaimOutcome::Applied:Message=TEXT("领取已保存，物品以同步的库存快照为准。");break;
    case EAetherLootClaimOutcome::AlreadyOwned:Message=TEXT("这份战利品已由你领取，不会重复发放。");break;
    case EAetherLootClaimOutcome::InventoryFull:Message=TEXT("背包空间不足，战利品仍留在原处；整理背包后可重试。");break;
    case EAetherLootClaimOutcome::ClaimedByOther:Message=TEXT("这份战利品已被其他玩家领取。");break;
    case EAetherLootClaimOutcome::Missing:Message=TEXT("这份战利品已不存在，请刷新目标。");break;
    case EAetherLootClaimOutcome::Invalid:Message=TEXT("领取未能确认，请稍后重试或重新同步库存。");break;
    default:return;
    }
    if(CompletedLootFeedback.Num()>=128)CompletedLootFeedback.RemoveAt(0);
    CompletedLootFeedback.Add(Key);
    // 已在拥有者客户端；直接走既有表现通知，不能再伪造普通持久命令回执。
    C->Notify_Implementation(Message);
}
void AAetherPlayerController::ClientV10Snapshot_Implementation(const FAetherV10SnapshotChunk& P)
{if(auto* LP=GetLocalPlayer())LP->GetSubsystem<UAetherCommandClient>()->ReceiveChunk(this,P);}

bool AAetherPlayerController::SendV10SceneInput(FAetherPlayerCommand C,FString& Why)
{
    auto* LP=GetLocalPlayer();auto* Client=LP?LP->GetSubsystem<UAetherCommandClient>():nullptr;
    if(!Client||!Client->GetChannel().IsValid()||Client->HasPending()){Why=TEXT("角色正在同步，请稍后再操作。");return false;}
    if(SceneInputChannel!=Client->GetChannel()){SceneInputChannel=Client->GetChannel();SceneInputSequence=0;}
    if(SceneInputSequence==MAX_uint64)return false;
    FAetherV10CommandPacket P;P.Channel=SceneInputChannel;if(!AetherCommands::Encode(C,P.Bytes,Why))return false;
    ServerV10SceneInput(P,++SceneInputSequence);Why=TEXT("已发送现场操作。");return true;
}
void AAetherPlayerController::ServerV10SceneInput_Implementation(const FAetherV10CommandPacket& P,uint64 Sequence)
{
    auto* GI=GetGameInstance();auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();FAetherPlayerCommand C;
    if(!GI||!M||!GI->GetSubsystem<UAetherCommandRuntime>()->AuthorizeSceneInput(this,P,Sequence,C))return;
    bool bLootClaimAccepted=false;
    const FString Result=M->ExecuteNativeSceneService(*this,C,bLootClaimAccepted);
    // 已接受掉落已有客户端“已发送”反馈。初始 Pawn RPC 与终态 Controller RPC
    // 不保证跨 Actor 到达顺序，因此这里只保留拒绝和其他场景服务的原提示。
    if(!bLootClaimAccepted)if(auto* CharacterPawn=Cast<AAetherFrontierCharacter>(GetPawn()))CharacterPawn->Notify(Result);
}

void AAetherPlayerController::ServerV10ContainerQuery_Implementation(const FAetherV10ContainerQuery& Q)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->QueryContainer(this,Q);}
void AAetherPlayerController::ClientV10ContainerClosed_Implementation(FGuid Channel,FGuid Context)
{if(auto* LP=GetLocalPlayer())LP->GetSubsystem<UAetherCommandClient>()->ReceiveContainerClosed(this,Channel,Context);}
