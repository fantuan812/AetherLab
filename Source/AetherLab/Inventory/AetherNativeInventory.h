#pragma once
#include "Inspection/AetherInspectionService.h"
#include "Contracts/AetherPlayerCommand.h"
class AAetherFrontierCharacter;
namespace AetherNativeInventory
{
    AETHERLAB_API void StatusEffects(AAetherFrontierCharacter& Character,TArray<FAetherInspectStatusEffect>& Out);
    AETHERLAB_API bool Snapshot(AAetherFrontierCharacter& Character,int64 PresentationRevision,FAetherInspectionSnapshot& Out);
    AETHERLAB_API bool Submit(AAetherFrontierCharacter& Character,FAetherPlayerCommand Command,int64 SeenRevision,FString& Reason);
    AETHERLAB_API bool Shortcut(AAetherFrontierCharacter& Character,FName Action,FName Definition,FString& Reason);
}
