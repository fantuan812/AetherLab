#pragma once
#include "Inspection/AetherInspectionService.h"
#include "Contracts/AetherPlayerCommand.h"
class AAetherFrontierCharacter;
namespace AetherNativeInventory
{
    AETHERGAMEPLAY_API void StatusEffects(AAetherFrontierCharacter& Character,TArray<FAetherInspectStatusEffect>& Out);
    AETHERGAMEPLAY_API bool Snapshot(AAetherFrontierCharacter& Character,int64 PresentationRevision,FAetherInspectionSnapshot& Out);
    AETHERGAMEPLAY_API bool Submit(AAetherFrontierCharacter& Character,FAetherPlayerCommand Command,int64 SeenRevision,FString& Reason);
    AETHERGAMEPLAY_API bool Shortcut(AAetherFrontierCharacter& Character,FName Action,FName Definition,FString& Reason);
}
