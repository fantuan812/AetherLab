#pragma once
#include "CoreMinimal.h"

// Current-schema startup is deliberately separate from any historical serialization.
namespace AetherSaveStartup
{
inline bool ValidPrefix(const FString& Prefix)
{
    if(Prefix.IsEmpty()||Prefix.Len()>64)return false;
    for(TCHAR C:Prefix)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='-'))return false;
    return true;
}
inline bool SelectPrefix(bool Specified,const FString& Requested,FString& InOutPrefix,FString& Reason)
{
    const FString& Candidate=Specified?Requested:InOutPrefix;
    if(!ValidPrefix(Candidate)){Reason=TEXT("AETHER_SAVE_PREFIX_INVALID: explicitly requested save name is invalid; no default namespace was opened.");return false;}
    if(Specified)InOutPrefix=Requested;
    Reason.Reset();return true;
}
inline bool CanOpen(bool HasCurrentWorld,bool HasOtherAggregates,bool HasHistoricalFiles,bool AllowFresh,FString& Reason)
{
    Reason.Reset();
    if(HasCurrentWorld)return true; // Caller must still validate the current schema and complete snapshot.
    if(HasOtherAggregates){Reason=TEXT("AETHER_SAVE_WORLD_MISSING: existing native records have no world; refusing replacement. Preserve the database and restore a verified native backup.");return false;}
    if(HasHistoricalFiles){Reason=TEXT("AETHER_SAVE_FORMAT_UNSUPPORTED: historical .sav/.crc files exist without a current native world. Automatic conversion and reset are disabled. Originals were not changed. Restore a current-schema backup, or explicitly choose a different save prefix for a new game.");return false;}
    if(!AllowFresh){Reason=TEXT("AETHER_SAVE_NEW_WORLD_NOT_AUTHORIZED: no current world exists; explicit new-world authorization is required.");return false;}
    return true;
}
}
