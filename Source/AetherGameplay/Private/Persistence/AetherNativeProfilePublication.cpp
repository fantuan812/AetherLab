#include "Framework/AetherProgression.h"
#include "Definitions/AetherV10Definitions.h"

bool AAetherPlayerState::PublishNativeProfile(const FAetherProfileStateV10& P,FString& Reason)
{
    check(IsInGameThread());const auto& D=FAetherV10Definitions::Get();
    if(!HasAuthority()||!D.bValid||!P.CharacterId.Equals(Profile.CharacterId,ESearchCase::CaseSensitive)||
        (NativeProfile.IsSet()&&P.Revision<NativeProfile->Revision)||!P.Validate(D.Items,D.Skills,D.Rules,Reason))
    {if(Reason.IsEmpty())Reason=TEXT("Invalid native profile publication");return false;}
    NativeProfile=P;bNativeSkillsEnabled=true;
    // 不扩展冻结的 v9 序列化结构。版本只供旧提示失效，原生命令使用完整 int64 DTO。
    FAetherProfile View;View.CharacterId=P.CharacterId;View.Revision=int32(FMath::Min<int64>(P.Revision,MAX_int32));
    View.Gold=P.Gold;View.Experience=P.Experience;View.bRegistered=P.bRegistered;View.bCompanion=P.bCompanion;
    View.LastAbbeyReceipt=P.LastAbbeyReceipt;View.LastRelayReceipt=P.LastRelayReceipt;View.DailyDate=P.DailyDate;
    for(const auto& Id:P.Evidence)View.Evidence.Add(FName(*Id));
    for(const auto& Id:P.Claims)View.Claims.Add(FName(*Id));
    for(const auto& Id:P.DailyEvidence)View.DailyEvidence.Add(FName(*Id));
    for(const auto& Id:P.DailyClaims)View.DailyClaims.Add(FName(*Id));
    // 旧任务导航只查询旧定义的数量。新实例状态/十槽装备仅走原生快照，不伪装成 v9 物品。
    for(const auto& Item:P.Inventory.Items)
        if(D.Rules.Items.Contains(FName(*Item.DefinitionId)))
        {FAetherItemStack V;V.InstanceId=Item.InstanceId;V.DefinitionId=FName(*Item.DefinitionId);V.Count=Item.Quantity;View.Inventory.Add(V);}
    for(const auto& Skill:D.Skills.Skills)if(Skill.Value.LegacyBit>=0&&Skill.Value.LegacyBit<4&&P.Skills.PermanentRank(Skill.Key)>0)View.LearnedSpells|=uint8(1<<Skill.Value.LegacyBit);
    Profile=MoveTemp(View);ForceNetUpdate();OnProfilePublished.Broadcast();Reason.Reset();return true;
}
