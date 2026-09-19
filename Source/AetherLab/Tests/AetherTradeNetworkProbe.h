#pragma once
#include "AetherInventoryCommand.h"
class AAetherFrontierCharacter;
namespace AetherTradeNetwork
{
    // 仅开发版专项联机夹具读取类型化结果；无测试标志时不记录，不参与玩法授权。
    void ObserveResult(AAetherFrontierCharacter* Character,FGuid Command,EAetherInventoryResult Result);
}
