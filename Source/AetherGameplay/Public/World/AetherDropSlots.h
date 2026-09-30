#pragma once
#include "Persistence/AetherWorldBootstrap.h"

// Scene-thread cache of audited/committed descriptors, including tombstones.
// Selection never consumes a slot: concurrent contenders are resolved by row CAS.
class FAetherDropSlots
{
public:
    bool Observe(const FAetherContainerRestoreDescriptor& C)
    {
        const auto* Old=Rows.Find(C.Id);
        if(Old&&C.Revision<Old->Revision)return false;
        if(Old&&C.Revision==Old->Revision)
            return C.Kind==Old->Kind&&C.bActive==Old->bActive&&C.Owner==Old->Owner&&C.Region==Old->Region&&C.Location==Old->Location;
        Rows.Add(C.Id,C);return true;
    }
    FString Resolve(const FGuid& Command) const
    {
        FString Selected;
        for(const auto& Pair:Rows)
        {
            const auto& C=Pair.Value;
            if(C.Kind==EAetherContainerKind::WorldDrop&&!C.bActive&&C.Revision<MAX_int64-1&&
                (Selected.IsEmpty()||C.Id.Compare(Selected,ESearchCase::CaseSensitive)<0))Selected=C.Id;
        }
        return Selected.IsEmpty()?TEXT("Drop_")+Command.ToString(EGuidFormats::Digits):Selected;
    }
    void Reset(){Rows.Reset();}
private:
    TMap<FString,FAetherContainerRestoreDescriptor> Rows;
};
