#pragma once
#include "CoreMinimal.h"
class AAetherFrontierProp;
class APlayerState;

// 仅属于当前 Pawn 的临时授权，绝不写入存档或复用到重生后的 Pawn。
// 持久库存请求不增加易失会话字段；服务器先确认旧回执，再要求新交易持有当前授权。
struct FAetherLiveTradeSession
{
    FGuid Token;
    TWeakObjectPtr<AAetherFrontierProp> Target;
    FName ShopId;
    FString CharacterId;
    TWeakObjectPtr<APlayerState> OwnerState;
};
struct FAetherSaleConfirmation
{
    FGuid Item,TradeToken;
    int32 ProfileRevision=-1,Quantity=0;
    float ExpiresAt=0;
};
