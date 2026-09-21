#include "Misc/AutomationTest.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherMenuStateTest,"Aether.V10.UI.MenuLifecycleAndStaleModal",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherMenuStateTest::RunTest(const FString&)
{
 auto* LP=NewObject<ULocalPlayer>(GEngine);auto* Menu=NewObject<UAetherMenuSubsystem>(LP);
 int32 Changes=0;Menu->OnChanged.AddLambda([&Changes](){++Changes;});
 Menu->TogglePage(EAetherMenuPage::Inventory);
 TestTrue(TEXT("Inventory opens"),Menu->IsOpen());
 Menu->TogglePage(EAetherMenuPage::Inventory);
 TestFalse(TEXT("Same shortcut closes"),Menu->IsOpen());
 Menu->TogglePage(EAetherMenuPage::Inventory);Menu->TogglePage(EAetherMenuPage::Skills);
 TestEqual(TEXT("Other shortcut changes page"),Menu->GetPage(),EAetherMenuPage::Skills);
 const auto Detail=Menu->PushLayer("SkillDetail"),Modal=Menu->PushLayer("RefundConfirmation");
 TestFalse(TEXT("Background detail cannot dismiss top modal"),Menu->DismissLayer(Detail));
 Menu->Back();TestEqual(TEXT("Escape unwinds modal first"),Menu->GetLayerCount(),1);
 TestFalse(TEXT("Late modal callback cannot dismiss detail"),Menu->DismissLayer(Modal));
 Menu->Back();TestTrue(TEXT("Detail closes before menu"),Menu->IsOpen());
 Menu->Back();TestFalse(TEXT("Escape closes current page, does not switch to system"),Menu->IsOpen());
 Menu->Back();TestEqual(TEXT("Escape while closed opens system"),Menu->GetPage(),EAetherMenuPage::System);
 FAetherMenuPageMemory Memory;Memory.Search=TEXT("药水");Memory.ScrollOffset=240;Memory.SelectedInstance=FGuid::NewGuid();
 Menu->SavePageMemory(EAetherMenuPage::Inventory,Memory);
 Menu->OpenPage(EAetherMenuPage::Inventory);Menu->Close();Menu->OpenPage(EAetherMenuPage::Journal);
 TestEqual(TEXT("Page search survives close and switch"),Menu->GetPageMemory(EAetherMenuPage::Inventory).Search,Memory.Search);
 TestEqual(TEXT("Page instance identity survives"),Menu->GetPageMemory(EAetherMenuPage::Inventory).SelectedInstance,Memory.SelectedInstance);
 TestEqual(TEXT("Separate page scroll"),Menu->GetPageMemory(EAetherMenuPage::Journal).ScrollOffset,0.f);
 const int32 Before=Changes;Menu->TogglePage(EAetherMenuPage(255));
 TestEqual(TEXT("Invalid page sends no event"),Changes,Before);
 const auto Stale=Menu->PushLayer("Old");Menu->OpenPage(EAetherMenuPage::Map);
 TestFalse(TEXT("Page switch invalidates modal token"),Menu->DismissLayer(Stale));
 TestEqual(TEXT("Page switch removes detail stack"),Menu->GetLayerCount(),0);
 Menu->OnChanged.Clear();

 auto* W=UWorld::CreateWorld(EWorldType::Game,false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
 ON_SCOPE_EXIT {Menu->OnChanged.Clear();Menu->AttachPawn(nullptr);W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false);};
 FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* First=W->SpawnActor<AAetherFrontierCharacter>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
 auto* Second=W->SpawnActor<AAetherFrontierCharacter>(FVector(500,0,0),FRotator::ZeroRotator,Params);
 Menu->AttachPawn(First);Menu->OpenPage(EAetherMenuPage::Inventory);
 First->bAttackHeld=true;First->Jump();
 Menu->Close();Menu->OpenPage(EAetherMenuPage::Map);
 TestFalse(TEXT("Opening map clears held attack"),First->bAttackHeld);
 TestFalse(TEXT("Opening map clears held jump"),First->bPressedJump);
 TestTrue(TEXT("Map blocks gameplay mirror too"),First->bPanel&&First->Panel==4);
 Menu->SavePageMemory(EAetherMenuPage::Inventory,Memory);
 const auto OldAvatarModal=Menu->PushLayer("OldAvatar");
 Menu->AttachPawn(Second);
 TestFalse(TEXT("Pawn replacement closes local menu"),Menu->IsOpen());
 TestFalse(TEXT("Old pawn released"),First->bPanel);
 TestFalse(TEXT("Replacement cannot inherit instance selection"),Menu->GetPageMemory(EAetherMenuPage::Inventory).SelectedInstance.IsValid());
 TestFalse(TEXT("Old avatar callback rejected"),Menu->DismissLayer(OldAvatarModal));
 Menu->OpenPage(EAetherMenuPage::Inventory);Second->Destroy();Menu->AttachPawn(nullptr);
 TestFalse(TEXT("Expired weak pawn still closes menu"),Menu->IsOpen());
 TestNull(TEXT("Disconnect releases weak avatar"),Menu->GetBoundPawn());
 return true;
}
#endif
