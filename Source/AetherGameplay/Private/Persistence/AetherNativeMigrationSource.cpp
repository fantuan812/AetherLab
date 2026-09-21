#include "Persistence/AetherNativeMigrationSource.h"
#include "Persistence/AetherLegacyV9Reader.h"
#include "Persistence/AetherLegacyWorldConverter.h"
#include "Definitions/AetherV10Definitions.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

bool AetherNativeMigration::ValidPrefix(const FString& Prefix)
{
    if(Prefix.IsEmpty()||Prefix.Len()>64)return false;
    for(TCHAR C:Prefix)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='-'))return false;
    return true;
}
FAetherNativeMigrationSource AetherNativeMigration::Prepare(const FString& Prefix)
{
    check(IsInGameThread());FAetherNativeMigrationSource Out;
    if(!ValidPrefix(Prefix)){Out.Detail=TEXT("Invalid save prefix");return Out;}
    const auto& D=FAetherV10Definitions::Get();if(!D.bValid){Out.Detail=D.Error;return Out;}
    struct FCandidate {int32 Generation=-1;FString Digest,Checksum,Name;TArray<uint8> Bytes;FAetherLegacyImport Import;};
    TOptional<FCandidate> Best;bool Any=false;FString Failures;
    for(int32 Slot=0;Slot<2;++Slot)
    {
        const FString Name=Prefix+FString::FromInt(Slot);
        const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("SaveGames")/(Name+TEXT(".sav")));
        const FString Sidecar=FPaths::ChangeExtension(Path,TEXT("crc"));
        const bool HasFile=IFileManager::Get().FileExists(*Path),HasSidecar=IFileManager::Get().FileExists(*Sidecar);
        Any|=HasFile||HasSidecar;if(!HasFile&&!HasSidecar)continue;
        auto Reject=[&](const FString& Why){Failures+=Name+TEXT(": ")+Why+TEXT("; ");};
        const int64 Size=IFileManager::Get().FileSize(*Path);
        if(!HasFile||Size<64||Size>16*1024*1024){Reject(TEXT("missing or oversized save"));continue;}
        FCandidate C;C.Name=Name;
        if(!FFileHelper::LoadFileToArray(C.Bytes,*Path)||C.Bytes.Num()!=Size){Reject(TEXT("save changed while reading"));continue;}
        auto Read=AetherLegacyV9::Read(C.Bytes);
        if(Read.Code!=EAetherLegacyReadCode::Ready||!Read.Snapshot.IsValid()){Reject(Read.Detail);continue;}
        if(Read.Snapshot->Version==5)
        {
            const FString Expected=FString::Printf(TEXT("%d:%u"),Read.Snapshot->Generation,FCrc::MemCrc32(C.Bytes.GetData(),C.Bytes.Num()));
            if(IFileManager::Get().FileSize(*Sidecar)>128||!FFileHelper::LoadFileToString(C.Checksum,*Sidecar)||C.Checksum!=Expected)
            {Reject(TEXT("committed-generation checksum mismatch"));continue;}
        }
        FSHA256Signature Hash;if(!SHA256(C.Bytes.GetData(),C.Bytes.Num(),Hash.Signature)){Reject(TEXT("SHA256 unavailable"));continue;}
        C.Digest=Hash.ToString().ToLower();C.Generation=Read.Snapshot->Generation;FString Reason;
        if(!AetherLegacyV9::ConvertSnapshot(*Read.Snapshot,C.Digest,D.Items,D.Skills,D.Rules,C.Import,Reason)){Reject(Reason);continue;}
        if(Best.IsSet()&&C.Generation==Best->Generation&&C.Digest!=Best->Digest)
        {Out.Detail=TEXT("Two different valid snapshots share one generation; manual recovery required");return Out;}
        if(!Best.IsSet()||C.Generation>Best->Generation)Best=MoveTemp(C);
    }
    if(!Any){Out.Code=EAetherNativeSourceCode::Missing;return Out;}
    if(!Best.IsSet()){Out.Detail=TEXT("No valid legacy generation; originals preserved. ")+Failures;return Out;}
    // 备份来自已校验的同一批字节，不重新复制可能已被其他进程修改的源文件。
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("V10Migration")/
        (FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    if(!IFileManager::Get().MakeDirectory(*Directory,true)){Out.Detail=TEXT("Cannot create migration backup directory");return Out;}
    const auto& B=Best.GetValue();
    if(!FFileHelper::SaveArrayToFile(B.Bytes,*(Directory/(B.Name+TEXT(".sav"))),&IFileManager::Get(),FILEWRITE_NoReplaceExisting)||
        (!B.Checksum.IsEmpty()&&!FFileHelper::SaveStringToFile(B.Checksum,*(Directory/(B.Name+TEXT(".crc"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_NoReplaceExisting)))
    {Out.Detail=TEXT("Migration backup failed; import not authorized");return Out;}
    TArray<uint8> BackupBytes;FString BackupChecksum;
    if(!FFileHelper::LoadFileToArray(BackupBytes,*(Directory/(B.Name+TEXT(".sav"))))||BackupBytes!=B.Bytes||
        (!B.Checksum.IsEmpty()&&(!FFileHelper::LoadFileToString(BackupChecksum,*(Directory/(B.Name+TEXT(".crc"))))||BackupChecksum!=B.Checksum)))
    {Out.Detail=TEXT("Migration backup readback failed; import not authorized");return Out;}
    const FString NL=LINE_TERMINATOR;
    const FString Manifest=TEXT("schema=1")+NL+TEXT("source=")+B.Name+NL+TEXT("generation=")+FString::FromInt(B.Generation)+NL+TEXT("sha256=")+B.Digest+
        NL+TEXT("legacyFilesRetained=true")+NL+TEXT("activated=false")+NL;
    if(!FFileHelper::SaveStringToFile(Manifest,*(Directory/TEXT("source.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_NoReplaceExisting))
    {Out.Detail=TEXT("Cannot write migration backup manifest");return Out;}
    Out.Code=EAetherNativeSourceCode::Ready;Out.BackupDirectory=Directory;Out.Import=MoveTemp(Best->Import);Out.Detail=Failures;return Out;
}
