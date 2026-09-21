#pragma once
#include "Interaction/AetherInteractionDefinitions.h"
class AAetherFrontierCharacter;
class AAetherFrontierProp;
namespace AetherNativeInteraction
{
    // 只读提供者：客户端使用已提交拥有者快照与当前复制场景，执行仍由服务器重新构建。
    AETHERGAMEPLAY_API TOptional<FAetherInteractionProvider> Provider(AAetherFrontierCharacter& Player,AAetherFrontierProp& Target);
    AETHERGAMEPLAY_API bool IsSceneService(EAetherInteractionActionKind Kind);
    AETHERGAMEPLAY_API bool IsPersistent(EAetherInteractionActionKind Kind);
    AETHERGAMEPLAY_API bool Submit(AAetherFrontierCharacter& Player,const FAetherInteractionSelection& Selection,FString& Reason);
}
