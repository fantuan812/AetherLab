#pragma once
#include "CoreMinimal.h"
#include "Interaction/AetherInteractionDefinitions.h"
class AAetherFrontierMode;
class AAetherFrontierCharacter;
class AAetherFrontierProp;
// 此上下文仅在入口已完成目标、权限、距离、版本复验后构建，不缓存 UObject 到工作线程。
struct FAetherSceneServiceContext
{
    AAetherFrontierMode& Mode;
    AAetherFrontierCharacter& Character;
    AAetherFrontierProp& Target;
    const FAetherInteractionActionDefinition& Action;
    double SafeForSeconds;
};
struct FAetherSceneServiceHandlers
{
    static bool Contains(EAetherInteractionActionKind Kind);
    static FString Execute(const FAetherSceneServiceContext& Context);
    static FString Rest(const FAetherSceneServiceContext& Context);
    static FString Trade(const FAetherSceneServiceContext& Context);
    static FString Train(const FAetherSceneServiceContext& Context);
    static FString BeginDaily(const FAetherSceneServiceContext& Context);
    static FString DrawWater(const FAetherSceneServiceContext& Context);
    static FString PourWater(const FAetherSceneServiceContext& Context);
    static FString Gate(const FAetherSceneServiceContext& Context);
    static FString StartEncounter(const FAetherSceneServiceContext& Context);
    static FString ChannelEncounter(const FAetherSceneServiceContext& Context);
    static FString CollectLegacyLoot(const FAetherSceneServiceContext& Context);
    static FString Recruit(const FAetherSceneServiceContext& Context);
};
