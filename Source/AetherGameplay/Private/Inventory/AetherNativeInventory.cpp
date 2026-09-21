#include "Inventory/AetherNativeInventory.h"
#include "Characters/AetherFrontierCharacter.h"
#include "World/AetherFrontierProp.h"
#include "World/AetherFrontierState.h"
#include "Networking/AetherCommandClient.h"
#include "Definitions/AetherV10Definitions.h"
#include "Contracts/AetherTransaction.h"
#include "Combat/AetherEquipmentMath.h"
#include "Engine/LocalPlayer.h"

namespace
{
UAetherCommandClient* Client(AAetherFrontierCharacter& C)
{
    auto* PC=Cast<APlayerController>(C.GetController());auto* LP=PC?PC->GetLocalPlayer():nullptr;
    return LP?LP->GetSubsystem<UAetherCommandClient>():nullptr;
}
}
bool AetherNativeInventory::Snapshot(AAetherFrontierCharacter& C,int64 Revision,FAetherInspectionSnapshot& Out)
{
    auto* Net=Client(C);if(!Net||!Net->GetProfile().IsSet()||!Net->GetChannel().IsValid())return false;
    const auto& P=Net->GetProfile().GetValue();Out={};Out.Context={P.CharacterId,Net->GetChannel(),Revision};
    Out.Container=Net->GetContainer();Out.ContainerContext=Net->GetContainerContext();Out.ContainerWorldRevision=Net->GetContainerWorldRevision();
    Out.ProfileRevision=P.Revision;Out.Inventory=P.Inventory;Out.Skills=P.Skills;Out.Gold=P.Gold;
    Out.ServerTimeSeconds=C.CombatTime();
    StatusEffects(C,Out.StatusEffects);
    if(const auto* PS=C.ProfileState();PS&&PS->SkillGrants.ProfileRevision==P.Revision)Out.ExternalGrants=PS->GetNativeSkillGrants();
    Out.bCanAct=C.Ready()&&!C.Carried&&!C.ReviveTarget&&!C.bTravelPending&&!Net->HasPending();
    if(auto* W=C.GetWorld()->GetGameState<AAetherFrontierState>())Out.WorldRevision=W->NativeWorldRevision;
    const auto Shop=C.ActiveShop();const auto* Definition=FAetherV10Definitions::Get().Economy.Shops.Find(Shop.ToString());
    if(!Shop.IsNone()&&Definition&&C.TradeSession.Target.IsValid())
    {Out.Shop=*Definition;Out.TradeTargetStableId=C.TradeSession.Target->Spec.Id.ToString();}
    return true;
}
bool AetherNativeInventory::Submit(AAetherFrontierCharacter& C,FAetherPlayerCommand Command,int64 Seen,FString& Why)
{
    auto* Net=Client(C);
    if(!Net||!Net->GetProfile().IsSet()||Net->GetProfile()->Revision!=Seen||Net->HasPending())
    {Why=TEXT("背包已变化或原请求仍待确认。");return false;}
    Command.ProtocolVersion=AetherCommands::LatestProtocolVersion;Command.ExpectedProfileRevision=Seen;
    Command.CommandId=AetherTransactions::NewCommandId(Seen);TArray<uint8> Bytes;
    if(!AetherCommands::Encode(Command,Bytes,Why))return false;
    if(!Net->Submit(Net->GetChannel(),Net->GetOwnerIdentity(),Bytes,Why))return false;
    Why=TEXT("操作已提交，等待服务器确认。");return true;
}
bool AetherNativeInventory::Shortcut(AAetherFrontierCharacter& C,FName Action,FName Definition,FString& Why)
{
    auto* Net=Client(C);if(!Net||!Net->GetProfile().IsSet()){Why=TEXT("等待原生背包同步。");return false;}
    const auto& P=Net->GetProfile().GetValue();const auto& Defs=FAetherV10Definitions::Get().Items;
    using E=EAetherCommandType;FAetherPlayerCommand Cmd;Cmd.ItemInstanceId=C.SelectedInstance;
    if(Action=="Next")
    {
        TArray<FGuid> Ordered;for(int32 Slot=0;Slot<P.Inventory.Capacity;++Slot)if(const auto* I=P.Inventory.At(Slot))Ordered.Add(I->InstanceId);
        if(Ordered.IsEmpty())return false;C.SelectedInstance=Ordered[(Ordered.Find(C.SelectedInstance)+1)%Ordered.Num()];return true;
    }
    if(Action=="Claim")
    {
        if(P.PendingRewards.IsEmpty()){Why=TEXT("没有待领取的奖励。");return false;}
        Cmd.Type=E::ClaimReward;Cmd.ItemInstanceId.Invalidate();Cmd.DefinitionId=TEXT("Reward.")+P.PendingRewards[0].RewardId.ToString(EGuidFormats::Digits);
        return Submit(C,MoveTemp(Cmd),P.Revision,Why);
    }
    if(Action=="Use")
    {
        Cmd.Type=E::UseItem;
        if(!Definition.IsNone())
        {
            const auto* I=P.Inventory.Items.FindByPredicate([&](const auto& V){return V.DefinitionId==Definition.ToString()&&!V.bLocked&&!P.Inventory.IsEquipped(V.InstanceId);});
            if(!I){Why=TEXT("没有可用的这种道具。");return false;}Cmd.ItemInstanceId=I->InstanceId;
        }
    }
    else if(Action=="CycleMain"||Action=="CycleOff")
    {
        Cmd.SlotId=Action=="CycleMain"?TEXT("MainHand"):TEXT("OffHand");
        const FGuid Current=P.Inventory.Equipment.FindRef(Cmd.SlotId);
        if(Action=="CycleOff"&&Current.IsValid()){Cmd.Type=E::UnequipItem;Cmd.ItemInstanceId=Current;Cmd.SlotId.Reset();}
        else
        {
            TArray<FGuid> Choices;for(int32 Slot=0;Slot<P.Inventory.Capacity;++Slot)
                if(const auto* I=P.Inventory.At(Slot))if(const auto* D=Defs.Items.Find(I->DefinitionId);D&&D->AllowedSlots.Contains(Cmd.SlotId))Choices.Add(I->InstanceId);
            if(Choices.IsEmpty()){Why=TEXT("没有可装备的物品。");return false;}
            Cmd.Type=E::EquipItem;Cmd.ItemInstanceId=Choices[(Choices.Find(Current)+1)%Choices.Num()];
        }
    }
    else if(Action=="Equip")
    {
        const auto* I=P.Inventory.Find(Cmd.ItemInstanceId);const auto* D=I?Defs.Items.Find(I->DefinitionId):nullptr;
        if(!D||D->AllowedSlots.IsEmpty()){Why=TEXT("先选择装备。");return false;}
        if(D->AllowedSlots.Num()!=1){Why=TEXT("请在物品详情中明确选择替换的戒指槽。");return false;}
        Cmd.Type=E::EquipItem;Cmd.SlotId=D->AllowedSlots[0];
    }
    else if(Action=="Unequip")Cmd.Type=E::UnequipItem;
    else if(Action=="Split"){Cmd.Type=E::SplitStack;Cmd.Quantity=C.InventoryQuantity;Cmd.DestinationIndex=P.Inventory.FirstEmpty();}
    else if(Action=="Merge"){Cmd.Type=E::MergeStack;Cmd.OtherInstanceId=C.MergeDestination;Cmd.Quantity=C.InventoryQuantity;}
    else if(Action=="Buy"||Action=="Sell")
    {
        if(C.ActiveShop().IsNone()||!C.TradeSession.Target.IsValid()){Why=TEXT("请先与商人开启交易。");return false;}
        Cmd.TargetStableId=C.TradeSession.Target->Spec.Id.ToString();Cmd.Quantity=C.InventoryQuantity;
        if(Action=="Buy"){Cmd.Type=E::BuyItem;Cmd.DefinitionId=Definition.ToString();Cmd.ItemInstanceId.Invalidate();}
        else Cmd.Type=E::SellItem;
    }
    else {Why=TEXT("不支持该背包快捷操作。");return false;}
    return Submit(C,MoveTemp(Cmd),P.Revision,Why);
}


void AetherNativeInventory::StatusEffects(AAetherFrontierCharacter& C,TArray<FAetherInspectStatusEffect>& Out)
{
    Out.Reset();auto* Net=Client(C);
    if(const auto* PS=C.ProfileState();PS&&Net&&Net->GetProfile().IsSet()&&PS->SkillGrants.ProfileRevision==Net->GetProfile()->Revision)
    {
        for(const auto& G:PS->SkillGrants.Rows)
        {
            if(G.Source!=uint8(EAetherSkillGrantSource::Temporary)||!G.InstanceId.IsValid()||G.ExpiresAtServerSeconds<=C.CombatTime())continue;
            const auto* D=FAetherV10Definitions::Get().Skills.Skills.Find(G.SkillId);if(!D)continue;
            FAetherInspectStatusEffect Effect;Effect.InstanceId=G.InstanceId;Effect.DefinitionId=G.SkillId;
            Effect.DisplayName=D->DisplayName+TEXT(" · 旅舍祝福");Effect.IconId=D->IconId;Effect.Source=TEXT("旅舍休息");
            Effect.ExpiresAtServerSeconds=G.ExpiresAtServerSeconds;
            Effect.Impacts.Add({TEXT("rank"),TEXT("授权等级"),FString::FromInt(G.Rank)});
            if(const auto* Rank=FAetherV10Definitions::Get().Skills.Effect(G.SkillId,G.Rank))
                for(const auto& Stat:Rank->PassiveStats)Effect.Impacts.Add({Stat.Key,Stat.Key,FString::Printf(TEXT("+%.1f"),Stat.Value)});
            Out.Add(MoveTemp(Effect));
        }
    }

    if(!C.CombatRuntime||!C.Reactive||!C.Attributes)return;
    const auto& State=C.Reactive->State;
    for(const auto& S:C.CombatRuntime->StatusEffects)
    {
        if(!S.InstanceId.IsValid()||(S.ExpiresAt>0&&S.ExpiresAt<=C.CombatTime()))continue;
        FAetherInspectStatusEffect E;E.InstanceId=S.InstanceId;E.DefinitionId=TEXT("Body.")+S.Kind.ToString();
        if(S.ExpiresAt>0)E.ExpiresAtServerSeconds=S.ExpiresAt;
        E.Source=TEXT("当前身体与环境状态");
        if(S.Kind==TEXT("Heat"))
        {
            E.DisplayName=TEXT("灼热");E.IconId=TEXT("Fire.Ignite");
            E.Impacts.Add({TEXT("temperature"),TEXT("体表温度"),FString::Printf(TEXT("%.1f °C"),State.TemperatureC)});
            const double Damage=FMath::Min(25.0,FMath::Max(0.0,(State.TemperatureC-55)*.12))*AetherEquipmentMath::ElementMultiplier(C.Attributes->GearFireResist.GetCurrentValue());
            E.Impacts.Add({TEXT("damage"),TEXT("当前每秒火伤害"),FString::Printf(TEXT("%.1f"),Damage)});
            E.Impacts.Add({TEXT("ends"),TEXT("解除条件"),TEXT("降温到 55 °C 以下")});
        }
        else if(S.Kind==TEXT("Frozen"))
        {
            E.DisplayName=TEXT("冻结");E.IconId=TEXT("Frost.Freeze");
            E.Impacts.Add({TEXT("ice"),TEXT("结冰比例"),FString::Printf(TEXT("%.0f%%"),State.IceFraction*100)});
            E.Impacts.Add({TEXT("movement"),TEXT("当前移速下降"),FString::Printf(TEXT("%.0f%%"),50*AetherEquipmentMath::ElementMultiplier(C.Attributes->GearFrostResist.GetCurrentValue()))});
            E.Impacts.Add({TEXT("ends"),TEXT("解除条件"),TEXT("融化到结冰比例不高于 50%")});
        }
        else if(S.Kind==TEXT("Wet"))
        {
            E.DisplayName=TEXT("潮湿");E.IconId=TEXT("Water.Draw");
            E.Impacts.Add({TEXT("wetness"),TEXT("导电湿润度"),FString::Printf(TEXT("%.0f%%"),State.ElectricalWetness01*100)});
            E.Impacts.Add({TEXT("shock"),TEXT("电击伤害增加"),FString::Printf(TEXT("%.1f%%"),State.ElectricalWetness01*25)});
            E.Impacts.Add({TEXT("ends"),TEXT("解除条件"),TEXT("身体干燥")});
        }
        else if(S.Kind==TEXT("Stun"))
        {
            E.DisplayName=TEXT("眩晕");E.IconId=TEXT("Storm.Strike");E.Source=TEXT("受击、招架反制或电击");
            E.Impacts.Add({TEXT("action"),TEXT("限制"),TEXT("暂时无法移动、攻击或施法")});
        }
        else continue;
        Out.Add(MoveTemp(E));
    }
}
