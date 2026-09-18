#include "World/AetherContainerState.h"
namespace
{
bool Id(const FString& S,bool Empty=false)
{
    if(S.IsEmpty())return Empty;if(S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))return false;
    return true;
}
}
bool FAetherContainerStateV10::Validate(const FAetherV10ItemDefinitions& D,FString& Reason) const
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(!D.Validate(Reason))return false;
    if(!Id(ContainerId)||!Id(RegionId,true)||Revision<0||Revision==MAX_int64||uint8(Kind)>2||
        Location.ContainsNaN()||Location.GetAbsMax()>1.e8||!Inventory.Equipment.IsEmpty())return Fail(TEXT("Invalid container identity/location/equipment"));
    if(Kind==EAetherContainerKind::PersonalStorage)
    {
        if(OwnerCharacterId.IsEmpty()||OwnerCharacterId.Len()>32)return Fail(TEXT("Personal storage needs owner"));
        for(TCHAR C:OwnerCharacterId)if(C<32)return Fail(TEXT("Invalid container owner"));
    }
    else if(!OwnerCharacterId.IsEmpty())return Fail(TEXT("Shared container cannot claim personal ownership"));
    if((!bActive&&(Kind!=EAetherContainerKind::WorldDrop||!Inventory.Items.IsEmpty()))||
        (bActive&&Kind==EAetherContainerKind::WorldDrop&&Inventory.Items.IsEmpty()))return Fail(TEXT("Invalid drop tombstone state"));
    for(const auto& Item:Inventory.Items)
        if(!Item.BoundToCharacter.IsEmpty()&&(Kind!=EAetherContainerKind::PersonalStorage||!Item.BoundToCharacter.Equals(OwnerCharacterId,ESearchCase::CaseSensitive)))
            return Fail(TEXT("Bound item cannot enter shared or another character's storage"));
    auto ContainerDefinitions=D;ContainerDefinitions.DefaultCapacity=Inventory.Capacity;
    if(!Inventory.Validate(ContainerDefinitions,Reason))return false;
    Reason.Reset();return true;
}
bool FAetherContainerStateV10::Allows(const FString& Character) const
{
    return bActive&&!Character.IsEmpty()&&(Kind!=EAetherContainerKind::PersonalStorage||OwnerCharacterId.Equals(Character,ESearchCase::CaseSensitive));
}
