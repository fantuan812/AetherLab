#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Networking/AetherV10Packets.h"
#include "AetherPlayerController.generated.h"

// 专服只发送原生 AHUD，真正的表现类由拥有该连接的客户端选择。
// 这样服务器无需为 HUD RPC 加载 UMG 或引用客户端脚本包。
UCLASS()
class AETHERLAB_API AAetherPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    // 服务器只从拥有此 Controller 的连接解析身份；包内只有通道和固定协议字节。
    UFUNCTION(Server,Reliable) void ServerV10Command(const FAetherV10CommandPacket& Packet);
    UFUNCTION(Server,Reliable) void ServerV10RequestSnapshot(FGuid Channel);
    UFUNCTION(Server,Reliable) void ServerV10SnapshotAck(FGuid Channel,FGuid Transfer,uint32 NextOffset);
    UFUNCTION(Client,Reliable) void ClientV10Channel(FGuid Channel,const FString& CanonicalOwner);
    UFUNCTION(Client,Reliable) void ClientV10Reply(const FAetherV10ReplyPacket& Packet);
    UFUNCTION(Client,Reliable) void ClientV10Snapshot(const FAetherV10SnapshotChunk& Chunk);
    virtual void ClientSetHUD_Implementation(TSubclassOf<AHUD> NewHUDClass) override;
    virtual void SpawnDefaultHUD() override;
    virtual void FlushPressedKeys() override;
    virtual void BeginPlay() override;
    virtual void SetPawn(APawn* InPawn) override;
    virtual void OnRep_Pawn() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void BindMenuPawn();
public:
    virtual bool ShouldFlushKeysWhenViewportFocusChanges() const override {return true;}
};
