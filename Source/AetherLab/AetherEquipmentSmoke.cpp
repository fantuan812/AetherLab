#include "AetherAdventure.h"
#include "AetherCombat.h"
#include "AetherContent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"

void AAetherAdventureMode::EquipmentSmokeStep()
{
    auto* P=Cast<AAetherCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); if (!P || Elapsed<2 || Elapsed<SmokeWait) return;
    auto* E=P->Equipment.Get(); auto* Target=Enemies.IsEmpty()?nullptr:Enemies[0].Get();
    if (!Target) { Check(false,TEXT("Equipment test target exists")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const FVector Start=LevelDefinition?LevelDefinition->PlayerSpawn.GetLocation():FVector(-850,0,110);
    auto Reset=[P](FVector Position)
    { P->ResetCombat();P->SetVitals(100,100,100);P->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);P->SetActorRotation(FRotator::ZeroRotator);P->GetController()->SetControlRotation(FRotator::ZeroRotator); };
    switch (SmokeStage++)
    {
    case 0:
    {
        Reset(Start); Target->ResetCombat();Target->SetVitals(100,100,100);Target->SetActorLocation(Start+FVector(135,0,0));
        Target->SetActorRotation(FRotator(0,180,0));
        Check(E->Catalog&&E->Catalog->IsValidCatalog(),TEXT("Native equipment data catalog is valid"));
        Check(P->GetMesh()->GetSkeletalMeshAsset()!=nullptr,TEXT("Imported unarmed character replaces primitive"));
        Check(E->VisualForSlot(TEXT("MainHand"))&&E->VisualForSlot(TEXT("OffHand")),TEXT("Sword and shield are independent mesh components"));
        if (auto* V=E->VisualForSlot(TEXT("MainHand")))
        {
            Check(V->GetAttachParent()==P->GetMesh()&&V->GetAttachSocketName()==TEXT("hand_r"),TEXT("Weapon attaches through character socket"));
            Check(V->Bounds.BoxExtent.GetMax()<150 && V->Bounds.BoxExtent.GetMax()>20,TEXT("Weapon centimetre scale is independent of skeleton unit scale"));
        }
        const int32 Revision=E->LoadoutRevision;
        Check(E->Equip(TEXT("OathSword"))&&E->LoadoutRevision==Revision,TEXT("Equipping current item is idempotent"));
        Check(!E->Equip(TEXT("UnknownWeapon"))&&E->LoadoutRevision==Revision,TEXT("Unknown item request cannot mutate loadout"));
        Check(E->Unequip(TEXT("MainHand")),TEXT("Unequip removes weapon independently"));
        Check(!E->VisualForSlot(TEXT("MainHand"))&&P->GetMesh()->GetSkeletalMeshAsset(),TEXT("Unequip preserves character body"));
        Check(!E->StartAttack(TEXT("Light"))&&P->Stamina()==100,TEXT("Unarmed rejected attack costs no stamina"));
        Check(E->Equip(TEXT("OathSword")),TEXT("Sword can be equipped again"));
        EquipmentHealthBefore=Target->Health();EquipmentHitsBefore=E->AppliedHitCount;
        Check(E->StartAttack(TEXT("Light"))&&P->Stamina()==92,TEXT("Accepted sword attack pays configured cost once"));
        Check(!E->StartAttack(TEXT("Light"))&&P->Stamina()==92,TEXT("Repeated request during attack is rejected without extra cost"));
        Check(!E->Equip(TEXT("TrainingHammer")),TEXT("Equipment cannot change inside attack transaction"));
        SmokeWait=Elapsed+.55f;break;
    }
    case 1:
        Check(FMath::IsNearlyEqual(EquipmentHealthBefore-Target->Health(),16.f),TEXT("Sword uses configured damage"));
        Check(E->AppliedHitCount==EquipmentHitsBefore+1,TEXT("Active window applies at most one hit per target"));
        Check(E->Equip(TEXT("TrainingHammer")),TEXT("Second weapon works without character subclass"));
        Check(!E->GuardDefinition()&&!E->InSlot(TEXT("OffHand")),TEXT("Two handed weapon clears conflicting shield slot"));
        Check(!E->Equip(TEXT("OathShield")),TEXT("Conflicting shield request is atomic"));
        P->ServerBlock(true);Check(!P->bBlocking,TEXT("Guard requires equipped guard capability"));
        Target->SetActorLocation(P->GetActorLocation()+FVector(280,0,0));Target->SetVitals(100,100,100);P->SetVitals(100,100,100);
        EquipmentHealthBefore=Target->Health();EquipmentHitsBefore=E->AppliedHitCount;
        Check(E->StartAttack(TEXT("Light")),TEXT("Hammer attack starts using data asset"));
        SmokeWait=Elapsed+.95f;break;
    case 2:
        Check(FMath::IsNearlyEqual(EquipmentHealthBefore-Target->Health(),28.f),TEXT("Hammer uses different damage and reach"));
        Check(E->AppliedHitCount==EquipmentHitsBefore+1,TEXT("Hammer active window does not duplicate damage"));
        E->Unequip(TEXT("MainHand"));P->ResetCombat();P->SetVitals(100,100,100);
        P->GetController()->SetControlRotation(FRotator(20,90,0));
        Check(P->TrySpell(0)&&P->Mana()<=82.01f,TEXT("GAS spell remains independent of equipped weapon"));
        SmokeWait=Elapsed+4.5f;break;
    case 3:
        Reset(Start);E->Equip(TEXT("OathSword"));E->Equip(TEXT("OathShield"));
        Target->ResetCombat();Target->SetVitals(100,100,100);Target->SetActorLocation(Start+FVector(135,0,0));Target->SetActorRotation(FRotator(0,180,0));
        P->ServerBlock(true);Check(P->bBlocking,TEXT("Equipped shield enables guard"));
        SmokeWait=Elapsed+.25f;break;
    case 4:
        EquipmentHealthBefore=P->Health();Target->PerformMelee(false);SmokeWait=Elapsed+.3f;break;
    case 5:
        Check(P->Health()==EquipmentHealthBefore&&P->Stamina()<95,TEXT("Shield intercepts hit and consumes guard stamina"));
        P->ServerBlock(false);Target->ResetCombat();Target->SetActorLocation(Target->Home);Reset(Start);SmokeWait=Elapsed+.4f;break;
    case 6:
    {
        Check(SaveAdventure(P).StartsWith(TEXT("Saved:")),TEXT("Version 2 checkpoint saves equipment"));
        E->Equip(TEXT("TrainingHammer"));
        auto* Save=Cast<UAetherAdventureSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot,0));
        auto* SavedPlayer=Save?Save->Characters.FindByPredicate([](const auto& C){return C.Id==TEXT("Player");}):nullptr;
        Check(SavedPlayer&&!SavedPlayer->Equipment.IsEmpty(),TEXT("Save uses stable equipment IDs"));
        if (SavedPlayer&&!SavedPlayer->Equipment.IsEmpty())
        {
            const auto Valid=SavedPlayer->Equipment;SavedPlayer->Equipment[0].ItemId=TEXT("UnknownWeapon");UGameplayStatics::SaveGameToSlot(Save,SaveSlot,0);
            Check(LoadAdventure(P).Contains(TEXT("equipment is invalid"))&&E->InSlot(TEXT("MainHand"))&&E->InSlot(TEXT("MainHand"))->ItemId==TEXT("TrainingHammer"),TEXT("Invalid saved equipment rejects before world mutation"));
            SavedPlayer->Equipment=Valid;UGameplayStatics::SaveGameToSlot(Save,SaveSlot,0);
        }
        Check(LoadAdventure(P).StartsWith(TEXT("Loaded:"))&&E->InSlot(TEXT("MainHand"))&&E->InSlot(TEXT("MainHand"))->ItemId==TEXT("OathSword")&&E->GuardDefinition(),TEXT("Loading restores sword and shield together"));
        if (LevelDefinition) Check(FindObject(TEXT("Sigil"))->Spec.ArtMesh.IsValid()&&Objects.Num()>20,TEXT("Authored abbey meshes bind to live quest and reactive actors"));
        SmokeWait=Elapsed+.2f;break;
    }
    case 7:
        Reset(Start);Target->ResetCombat();Target->SetVitals(100,100,100);Target->SetActorLocation(Start+FVector(135,0,0));
        EquipmentHealthBefore=Target->Health();EquipmentHitsBefore=E->AppliedHitCount;
        Check(E->StartAttack(TEXT("Light")),TEXT("Attack begins before interruption"));
        P->ReceiveHit(1,100,Target,false);
        Check(E->Attack.bCancelled&&!E->IsBusy(),TEXT("Posture break cancels attack immediately"));
        SmokeWait=Elapsed+.3f;break;
    case 8:
        Check(Target->Health()==EquipmentHealthBefore&&E->AppliedHitCount==EquipmentHitsBefore,TEXT("Cancelled attack cannot deliver delayed hit"));
        SmokeWait=Elapsed+.1f;break;
    default:
        UE_LOG(LogTemp,Display,TEXT("AETHER_EQUIPMENT_SMOKE_%s failures=%d"),SmokeFailures?TEXT("FAIL"):TEXT("PASS"),SmokeFailures);
        FPlatformMisc::RequestExitWithStatus(false,SmokeFailures?1:0);break;
    }
}
