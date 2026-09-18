#include "AetherPersistence.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "HAL/FileManager.h"
namespace
{
class FLocalSnapshotStore final : public IAetherSnapshotStore
{
 FString Base(const FString& Slot) const {return FPaths::ProjectSavedDir()/TEXT("SaveGames")/Slot;}
public:
 bool Exists(const FString& Slot) const override {return UGameplayStatics::DoesSaveGameExist(Slot,0);}
 USaveGame* Load(const FString& Slot) const override {return UGameplayStatics::LoadGameFromSlot(Slot,0);}
 bool IsCommitted(const FString& Slot,int32 Generation) const override
 {
  TArray<uint8> Bytes;FString Checksum;
  return FFileHelper::LoadFileToArray(Bytes,*(Base(Slot)+TEXT(".sav")))&&FFileHelper::LoadFileToString(Checksum,*(Base(Slot)+TEXT(".crc")))
   &&Checksum==FString::Printf(TEXT("%d:%u"),Generation,FCrc::MemCrc32(Bytes.GetData(),Bytes.Num()));
 }
 bool Publish(USaveGame* Snapshot,const FString& Slot,int32 Generation,bool FailAfterData) override
 {
  const FString Path=Base(Slot);
  if(!IFileManager::Get().Delete(*(Path+TEXT(".crc")),false,true,true))return false;
  if(!UGameplayStatics::SaveGameToSlot(Snapshot,Slot,0)||FailAfterData)return false;
  if(!Load(Slot))return false;
  TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*(Path+TEXT(".sav"))))return false;
  const FString Checksum=FString::Printf(TEXT("%d:%u"),Generation,FCrc::MemCrc32(Bytes.GetData(),Bytes.Num()));
  if(!FFileHelper::SaveStringToFile(Checksum,*(Path+TEXT(".crc.pending")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))return false;
  return IFileManager::Get().Move(*(Path+TEXT(".crc")),*(Path+TEXT(".crc.pending")),true,true);
 }
};
}
TSharedRef<IAetherSnapshotStore> AetherLocalSnapshotStore(){return MakeShared<FLocalSnapshotStore>();}
