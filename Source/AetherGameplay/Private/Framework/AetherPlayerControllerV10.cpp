#include "Framework/AetherPlayerController.h"
#include "Networking/AetherCommandRuntime.h"
#include "Networking/AetherCommandClient.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Framework/AetherFrontierMode.h"
#include "Characters/AetherFrontierCharacter.h"

void AAetherPlayerController::ServerV10Command_Implementation(const FAetherV10CommandPacket& P)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->Receive(this,P);}
void AAetherPlayerController::ServerV10RequestSnapshot_Implementation(FGuid Channel)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->RequestSnapshot(this,Channel);}
void AAetherPlayerController::ServerV10SnapshotAck_Implementation(FGuid Channel,FGuid Transfer,uint32 NextOffset)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->AcknowledgeSnapshot(this,Channel,Transfer,NextOffset);}
void AAetherPlayerController::ClientV10Channel_Implementation(FGuid Channel,const FString& CanonicalOwner,FGuid Realm)
{if(auto* LP=GetLocalPlayer())LP->GetSubsystem<UAetherCommandClient>()->ReceiveChannel(this,Channel,CanonicalOwner,Realm);}
void AAetherPlayerController::ClientV10Reply_Implementation(const FAetherV10ReplyPacket& P)
{if(auto* LP=GetLocalPlayer())LP->GetSubsystem<UAetherCommandClient>()->ReceiveReply(this,P);}
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
    const FString Result=M->ExecuteNativeSceneService(*this,C);
    if(auto* CharacterPawn=Cast<AAetherFrontierCharacter>(GetPawn()))CharacterPawn->Notify(Result);
}

void AAetherPlayerController::ServerV10ContainerQuery_Implementation(const FAetherV10ContainerQuery& Q)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->QueryContainer(this,Q);}
void AAetherPlayerController::ClientV10ContainerClosed_Implementation(FGuid Channel,FGuid Context)
{if(auto* LP=GetLocalPlayer())LP->GetSubsystem<UAetherCommandClient>()->ReceiveContainerClosed(this,Channel,Context);}
