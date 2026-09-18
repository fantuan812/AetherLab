#include "Contracts/AetherTransactionalStore.h"
bool AetherImports::Validate(const FAetherLegacyImport& Import,FString& Reason)
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if((Import.SourceSchema!=4&&Import.SourceSchema!=5)||Import.SourceSha256.Len()!=64||Import.Values.IsEmpty()||Import.Values.Num()>MaxValues)
        return Fail(TEXT("Invalid legacy import schema/source/count"));
    for(TCHAR C:Import.SourceSha256)if(!((C>='0'&&C<='9')||(C>='a'&&C<='f')))return Fail(TEXT("Invalid legacy SHA256"));
    TSet<FAetherAggregateKey> Keys;int64 Bytes=0;int32 Worlds=0;
    for(const auto& V:Import.Values)
    {
        if((V.Key.Kind!=EAetherAggregateKind::Profile&&V.Key.Kind!=EAetherAggregateKind::World)||V.Key.Id.IsEmpty()||V.Key.Id.Len()>128||
            Keys.Contains(V.Key)||V.Revision<0||V.Revision==MAX_int64||V.SchemaVersion!=AetherTransactions::SchemaVersion||
            V.Payload.IsEmpty()||V.Payload.Num()>AetherTransactions::MaxPayloadBytes)
            return Fail(TEXT("Invalid or duplicate import aggregate"));
        FTCHARToUTF8 Encoded(*V.Key.Id);FUTF8ToTCHAR Decoded(Encoded.Get(),Encoded.Length());
        if(FString(Decoded.Length(),Decoded.Get())!=V.Key.Id)return Fail(TEXT("Import identity is not lossless UTF-8"));
        for(TCHAR C:V.Key.Id)if(C<32)return Fail(TEXT("Control character in import identity"));
        Keys.Add(V.Key);Bytes+=V.Payload.Num();Worlds+=V.Key.Kind==EAetherAggregateKind::World?1:0;
        if(Bytes>MaxBytes)return Fail(TEXT("Import payload exceeds memory bound"));
    }
    if(Worlds!=1)return Fail(TEXT("Legacy import requires exactly one complete world aggregate"));
    Reason.Reset();return true;
}
