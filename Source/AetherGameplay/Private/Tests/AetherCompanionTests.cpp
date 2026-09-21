#include "Misc/AutomationTest.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherCompanionHealTest,"Aether.V10.Party.HealingMembershipAndResourceConservation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherCompanionHealTest::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 if(!TestNotNull(TEXT("Isolated party world"),World))return false;
 ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);};
 const auto Spawn=[&]{
  FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  auto* C=World->SpawnActor<AAetherFrontierCharacter>(FVector::ZeroVector,FRotator::ZeroRotator,P);
  C->SetActorEnableCollision(false);C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());
  C->AbilitySystem->InitAbilityActorInfo(C,C);C->SetVitals(40,100,100);return C;
 };
 auto* Owner=Spawn();auto* Healer=Spawn();auto* Guard=Spawn();auto* Stranger=Spawn();
 Healer->CompanionOwner=Owner;Healer->bHealer=true;Guard->CompanionOwner=Owner;
 // 直接调用生产治疗提交入口，检查真正的 GAS 生命/法力，不复制治疗公式当作测试。
 Healer->ExecuteCompanionHeal(Guard);
 TestEqual(TEXT("Wounded guard receives heal"),Guard->Health(),60.f);
 TestEqual(TEXT("Healer pays once"),Healer->Mana(),85.f);
 Healer->ExecuteCompanionHeal(Healer);
 TestEqual(TEXT("Healer may heal own wounds"),Healer->Health(),60.f);
 TestEqual(TEXT("Self heal pays once"),Healer->Mana(),70.f);
 Healer->ExecuteCompanionHeal(Stranger);
 TestEqual(TEXT("Unrelated party cannot receive heal"),Stranger->Health(),40.f);
 TestEqual(TEXT("Rejected target costs nothing"),Healer->Mana(),70.f);
 Guard->SetActorLocation(FVector(700,0,0));Healer->ExecuteCompanionHeal(Guard);
 TestEqual(TEXT("Out of reach costs nothing"),Healer->Mana(),70.f);
 Healer->SetVitals(60,10,100);Healer->ExecuteCompanionHeal(Owner);
 TestEqual(TEXT("Insufficient mana cannot heal owner"),Owner->Health(),40.f);
 TestEqual(TEXT("No negative mana"),Healer->Mana(),10.f);
 Healer->SetVitals(60,100,100);Owner->SetVitals(0,100,100);Healer->ExecuteCompanionHeal(Owner);
 TestEqual(TEXT("Heal cannot bypass downed rescue channel"),Owner->Health(),0.f);
 TestEqual(TEXT("Downed rejection costs nothing"),Healer->Mana(),100.f);
 return true;
}
#endif
