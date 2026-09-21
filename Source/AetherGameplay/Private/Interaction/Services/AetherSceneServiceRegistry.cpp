#include "Interaction/AetherSceneServiceHandlers.h"
namespace
{
using FHandler=FString(*)(const FAetherSceneServiceContext&);
const TMap<EAetherInteractionActionKind,FHandler>& Registry()
{
    using K=EAetherInteractionActionKind;
    static const TMap<EAetherInteractionActionKind,FHandler> Entries={
        {K::Rest,&FAetherSceneServiceHandlers::Rest},
        {K::Train,&FAetherSceneServiceHandlers::Train},
        {K::Trade,&FAetherSceneServiceHandlers::Trade},
        {K::Repair,&FAetherSceneServiceHandlers::Trade},
        {K::BeginDaily,&FAetherSceneServiceHandlers::BeginDaily},
        {K::DrawWater,&FAetherSceneServiceHandlers::DrawWater},
        {K::PourWater,&FAetherSceneServiceHandlers::PourWater},
        {K::OpenGate,&FAetherSceneServiceHandlers::Gate},
        {K::CloseGate,&FAetherSceneServiceHandlers::Gate},
        {K::StartEncounter,&FAetherSceneServiceHandlers::StartEncounter},
        {K::ChannelEncounter,&FAetherSceneServiceHandlers::ChannelEncounter},
        {K::CollectLegacyLoot,&FAetherSceneServiceHandlers::CollectLegacyLoot},
        {K::RecruitGuard,&FAetherSceneServiceHandlers::Recruit},
        {K::RecruitHealer,&FAetherSceneServiceHandlers::Recruit}
    };
    return Entries;
}
}
bool FAetherSceneServiceHandlers::Contains(EAetherInteractionActionKind Kind){return Registry().Contains(Kind);}
FString FAetherSceneServiceHandlers::Execute(const FAetherSceneServiceContext& Context)
{
    const auto* Handler=Registry().Find(Context.Action.Kind);
    return Handler?(*Handler)(Context):TEXT("服务不可用。");
}
