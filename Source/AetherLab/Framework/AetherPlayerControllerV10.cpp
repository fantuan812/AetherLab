#include "Framework/AetherPlayerController.h"
#include "Networking/AetherCommandRuntime.h"
#include "Networking/AetherCommandClient.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

void AAetherPlayerController::ServerV10Command_Implementation(const FAetherV10CommandPacket& P)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->Receive(this,P);}
void AAetherPlayerController::ServerV10RequestSnapshot_Implementation(FGuid Channel)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->RequestSnapshot(this,Channel);}
void AAetherPlayerController::ServerV10SnapshotAck_Implementation(FGuid Channel,FGuid Transfer,uint32 NextOffset)
{if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherCommandRuntime>()->AcknowledgeSnapshot(this,Channel,Transfer,NextOffset);}
void AAetherPlayerController::ClientV10Channel_Implementation(FGuid Channel,const FString& Owner)
{if(auto* LP=GetLocalPlayer())LP->GetSubsystem<UAetherCommandClient>()->ReceiveChannel(this,Channel,Owner);}
void AAetherPlayerController::ClientV10Reply_Implementation(const FAetherV10ReplyPacket& P)
{if(auto* LP=GetLocalPlayer())LP->GetSubsystem<UAetherCommandClient>()->ReceiveReply(this,P);}
void AAetherPlayerController::ClientV10Snapshot_Implementation(const FAetherV10SnapshotChunk& P)
{if(auto* LP=GetLocalPlayer())LP->GetSubsystem<UAetherCommandClient>()->ReceiveChunk(this,P);}
