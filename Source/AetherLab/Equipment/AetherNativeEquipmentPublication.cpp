#include "Equipment/AetherEquipmentEffect.h"
#include "AetherProgression.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Definitions/AetherV10Definitions.h"

bool AAetherPlayerState::PublishNativeEquipment(const FAetherProfileStateV10& P,FString& Reason)
{
    check(IsInGameThread());const auto& D=FAetherV10Definitions::Get();
    auto* Pawn=Cast<AAetherFrontierCharacter>(GetPawn());
    if(!HasAuthority()||bPublishingNativeEquipment||!D.bValid||!Pawn||!AbilitySystem||AbilitySystem->GetAvatarActor()!=Pawn||
        !P.CharacterId.Equals(Profile.CharacterId,ESearchCase::CaseSensitive)||P.Revision<NativeEquipmentRevision||
        !P.Validate(D.Items,D.Skills,D.Rules,Reason))
    {if(Reason.IsEmpty())Reason=TEXT("Equipment publication requires the current committed avatar");return false;}
    // 先验证完整外观投影，缺内容时不把部分槽当作恢复成功。
    if(!Pawn->Equipment||!Pawn->Equipment->Catalog){Reason=TEXT("Equipment catalog unavailable");return false;}
    TArray<FAetherEquippedSlot> Loadout;
    for(const auto& Binding:P.Inventory.Equipment)
    {
        const auto* Instance=P.Inventory.Find(Binding.Value);
        const auto* Definition=Instance?D.Items.Items.Find(Instance->DefinitionId):nullptr;
        if(!Definition){Reason=TEXT("Committed equipment instance missing");return false;}
        FAetherEquippedSlot Slot;Slot.Slot=FName(*Binding.Key);Slot.InstanceId=Instance->InstanceId;
        Slot.ItemId=FName(*(Definition->EquipmentId.IsEmpty()?Definition->Id:Definition->EquipmentId));Loadout.Add(Slot);
    }
    Loadout.Sort([](const auto& A,const auto& B){return A.Slot.LexicalLess(B.Slot);});
    if(!Pawn->Equipment->ValidateLoadout(Loadout)){Reason=TEXT("Native equipment assets or slot definitions unavailable");return false;}
    TGuardValue<bool> Guard(bPublishingNativeEquipment,true);NativeEquipmentRevision=P.Revision;
    const float HP=Pawn->Health(),MP=Pawn->Mana(),SP=Pawn->Stamina();
    if(!AetherEquipmentEffects::Publish(*AbilitySystem,NativeEquipmentSource,P.Inventory.EquippedStats(D.Items),Reason))return false;
    if(GetPawn()!=Pawn||AbilitySystem->GetAvatarActor()!=Pawn){Reason=TEXT("Avatar changed while equipment effects were publishing");return false;}
    // 上限下降夹取；上限上升也不补血，换装不能绕过药剂或复活规则。
    Pawn->MaxHealth=FMath::Clamp(100.f+Attributes->GearMaxHealth.GetCurrentValue(),1.f,100000.f);
    Pawn->SetVitals(FMath::Min(HP,Pawn->Health()),FMath::Min(MP,Pawn->Mana()),FMath::Min(SP,Pawn->Stamina()));
    Pawn->Equipment->bProfileManaged=true;
    if(!Pawn->Equipment->RestoreLoadout(Loadout)){Reason=TEXT("Native loadout publication failed");return false;}
    if(GetPawn()!=Pawn||AbilitySystem->GetAvatarActor()!=Pawn){Reason=TEXT("Avatar changed while publishing loadout");return false;}
    Pawn->OnAppearanceChanged.Broadcast();Pawn->ForceNetUpdate();Reason.Reset();return true;
}
