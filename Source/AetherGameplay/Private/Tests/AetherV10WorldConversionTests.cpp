#include "Misc/AutomationTest.h"
#include "Persistence/AetherLegacyV9Reader.h"
#include "Persistence/AetherLegacyWorldConverter.h"
#include "Persistence/AetherSqliteStore.h"
#include "World/AetherWorldCodec.h"
#include "Profile/AetherProfileCodec.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherWorldConversionTest,"Aether.V10.Migration.WorldConversionAndExplicitDTO",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherWorldConversionTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);const auto& Rules=FAetherRules::Get();
    const FString Digest=FString::ChrN(64,'a');
    TStrongObjectPtr<UAetherFrontierSave> Old(NewObject<UAetherFrontierSave>());Old->Generation=23;
    FAetherProfile P;P.CharacterId=TEXT("合成角色");P.Revision=7;Old->Profiles.Add(P);
    const TMap<FString,int64> Profiles={{P.CharacterId,7}};
    Old->bSupplyRestored=true;Old->bWorkshopRestored=true;Old->bBridgeReleased=true;Old->bPowerOn=false;
    Old->RainKgPerM2Sec=.2;Old->AmbientTemperatureC=-17;Old->WindMPerSec=FVector(3,4,5);
    FReactiveSaveRecord B;B.RegionId=TEXT("Town");B.StableId=TEXT("SavedBox");
    B.Transform=FTransform(FRotator(10,20,30),FVector(100,200,300),FVector(2,3,4));
    B.bGateOpen=true;B.bHasMechanism=true;B.bSupportReleased=true;B.bSourceEnabled=false;
    B.RemainingEnergyJ=45.25;B.SourceAge=16.5;B.MaterialSignature=0xfedcba98;B.MaterialSchema=1;
    B.EnthalpyJ=-1234.5;B.WaterKg=.4;B.ElectricalWaterKg=.2;B.ElectricalWetness01=.5;B.FuelKg=.7;
    B.Integrity=.02;B.bBroken=true;B.bBurst=true;Old->World.Add(B);
    FAetherWorldLoot L;L.ClaimId=FGuid(1,2,3,4);L.Location=FVector(11,22,33);L.Count=7;
    L.Items.Add(TEXT("Potion"),2);L.Items.Add(TEXT("Material"),3);L.ClaimedBy=P.CharacterId;Old->Loot.Add(L);
    FAetherCampReceipt C;for(const auto& E:Rules.Encounters){C.Definition=E.Key;break;}
    C.Instance=FGuid(2,3,4,5);C.RespawnAfterUtc=1800000000;Old->CampReceipts.Add(C);
    FAetherWorldServiceReceipt Receipt;Receipt.CharacterId=P.CharacterId;Receipt.Command.Id=FGuid(3,4,5,6);
    Receipt.Command.TargetId=TEXT("PowerSource");Receipt.Command.ExpectedRevision=6;Old->ServiceReceipts.Add(Receipt);
    for(const auto& F:Rules.Objectives)if(F.Value.Scope==EAetherObjectiveScope::World&&!F.Value.FactSources.IsEmpty())
    {Old->WorldFacts.Sources.Add(F.Key,F.Value.FactSources[0]);break;}
    Old->Abbey.Definition=TEXT("Abbey");Old->Abbey.Instance=FGuid(5,6,7,8);Old->Abbey.Phase=EAetherEncounterPhase::Succeeded;
    Old->Abbey.Version=12;Old->Abbey.Wave=3;Old->Abbey.LockedSeats=4;Old->Abbey.PhaseStarted=71.25;Old->Abbey.Progress=9;
    Old->Abbey.Participants={P.CharacterId,TEXT("Other")};Old->Abbey.Settled={P.CharacterId};
    FAetherWorldStateV10 S,Out;TArray<uint8> Bytes,Again;
    if(!TestTrue(*Reason,AetherLegacyV9::ConvertWorld(*Old,Digest,Items,Rules,Profiles,S,Reason)))return false;
    if(!TestTrue(*Reason,AetherWorldCodec::Encode(S,Items,Rules,Profiles,Bytes,Reason)&&AetherWorldCodec::Decode(Bytes,Items,Rules,Profiles,Out,Reason)))return false;
    TestTrue(TEXT("Generation and all environment flags retained"),Out.Revision==23&&Out.LegacyGeneration==23&&Out.bSupplyRestored&&Out.bWorkshopRestored&&Out.bBridgeReleased&&!Out.bPowerOn&&Out.WindMPerSec==Old->WindMPerSec&&Out.AmbientTemperatureC==-17&&Out.RainKgPerM2Sec==.2);
    if(!TestEqual(TEXT("No physical record dropped"),Out.Bodies.Num(),1))return false;
    const auto& R=Out.Bodies[0];
    TestTrue(TEXT("Transform and mechanism state retained"),R.Transform.Equals(B.Transform,0)&&R.bGateOpen&&R.bHasMechanism&&R.bSupportReleased&&!R.bSourceEnabled&&R.RemainingEnergyJ==45.25&&R.SourceAge==16.5);
    TestTrue(TEXT("Material signature and thermodynamic scalars retained"),R.MaterialSignature==B.MaterialSignature&&R.MaterialSchema==1&&R.EnthalpyJ==-1234.5&&R.WaterKg==.4&&R.ElectricalWaterKg==.2&&R.ElectricalWetness01==.5&&R.FuelKg==.7&&R.Integrity==.02&&R.bBroken&&R.bBurst&&R.GasEnergyJ==0);
    TestTrue(TEXT("Claimed loot cannot reappear as unclaimed"),Out.Loot.Num()==1&&Out.Loot[0].ClaimId==L.ClaimId&&Out.Loot[0].ClaimedBy==P.CharacterId&&Out.Loot[0].Items.FindRef(TEXT("Potion"))==2&&Out.Loot[0].Count==7);
    TestTrue(TEXT("Camp and old service receipts preserved"),Out.CampReceipts[0].Instance==C.Instance&&Out.CampReceipts[0].RespawnAfterUtc==C.RespawnAfterUtc&&Out.LegacyServiceReceipts[0].CommandId==Receipt.Command.Id);
    TestTrue(TEXT("World fact provenance and encounter settlement preserved"),Out.WorldFactSources.Num()==Old->WorldFacts.Sources.Num()&&Out.Abbey.Phase==5&&Out.Abbey.Instance==Old->Abbey.Instance&&Out.Abbey.Wave==3&&Out.Abbey.Settled==Old->Abbey.Settled&&Out.Abbey.Participants==Old->Abbey.Participants);
    TestTrue(TEXT("All fields roundtrip byte exactly"),AetherWorldCodec::Encode(Out,Items,Rules,Profiles,Again,Reason)&&Again==Bytes);
    for(int32 N=0;N<Bytes.Num();++N)
    {
        TArray<uint8> Cut;Cut.Append(Bytes.GetData(),N);
        TestFalse(TEXT("Every world truncation rejected"),AetherWorldCodec::Decode(Cut,Items,Rules,Profiles,Out,Reason));
        TestEqual(TEXT("Invalid input never replaces caller state"),Out.Revision,int64(23));
    }
    Again=Bytes;Again.Add(0);TestFalse(TEXT("Trailing bytes rejected"),AetherWorldCodec::Decode(Again,Items,Rules,Profiles,Out,Reason));
    Again=Bytes;Again[4]=99;TestFalse(TEXT("Future world schema rejected"),AetherWorldCodec::Decode(Again,Items,Rules,Profiles,Out,Reason));
    Again=Bytes;Again[57]=255;Again[58]=255;TestFalse(TEXT("Body count rejected before allocation"),AetherWorldCodec::Decode(Again,Items,Rules,Profiles,Out,Reason));
    // 首个 double 是降雨；注入 IEEE quiet NaN，不能经过数学类型构造器变成零。
    Again=Bytes;for(int32 I=0;I<8;++I)Again[17+I]=0;Again[23]=0xf8;Again[24]=0x7f;
    TestFalse(TEXT("Nonfinite encoded physical scalar rejected"),AetherWorldCodec::Decode(Again,Items,Rules,Profiles,Out,Reason));
    auto Bad=S;const auto Copy=Bad.Bodies[0];Bad.Bodies.Add(Copy);TestFalse(TEXT("Duplicate physical identity rejected"),Bad.Validate(Items,Rules,Profiles,Reason));
    Bad=S;Bad.Bodies[0].ElectricalWaterKg=5;TestFalse(TEXT("Electrical water cannot exceed total"),Bad.Validate(Items,Rules,Profiles,Reason));
    Bad=S;Bad.Abbey.Settled.Add(TEXT("NeverJoined"));TestFalse(TEXT("Settlement must belong to a participant"),Bad.Validate(Items,Rules,Profiles,Reason));
    TestFalse(TEXT("Service receipt cannot reference missing profile"),S.Validate(Items,Rules,{},Reason));
    Old->Version=4;TestTrue(TEXT("v4 physical world is retained"),AetherLegacyV9::ConvertWorld(*Old,Digest,Items,Rules,Profiles,Out,Reason)&&Out.Bodies.Num()==1&&Out.LegacySaveSchema==4);
    Old->RainKgPerM2Sec=2;TestFalse(TEXT("Invalid old environment is diagnosed, not clamped"),AetherLegacyV9::ConvertWorld(*Old,Digest,Items,Rules,Profiles,Out,Reason));
    TestEqual(TEXT("Failed conversion keeps prior result"),Out.RainKgPerM2Sec,.2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherFullSnapshotImportTest,"Aether.V10.Migration.CompleteSnapshotAtomicImport",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherFullSnapshotImportTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);const auto& Rules=FAetherRules::Get();const auto& Skills=FAetherSkillDefinitionsV10::Get();
    TArray<uint8> Fixture;FFileHelper::LoadFileToArray(Fixture,*(FPaths::ProjectDir()/TEXT("Docs/Fixtures/V9/Profiles.sav")));
    auto Read=AetherLegacyV9::Read(Fixture);
    if(!TestTrue(*Read.Detail,Read.Code==EAetherLegacyReadCode::Ready))return false;
    FAetherLegacyImport Import;
    if(!TestTrue(*Reason,AetherLegacyV9::ConvertSnapshot(*Read.Snapshot,TEXT("9e2b795785ba0d4e8da95f5baf01425db56fcaf3ba861fc2a24dc5733621b693"),Items,Skills,Rules,Import,Reason)))return false;
    TestEqual(TEXT("All nineteen profiles and one world form a single import"),Import.Values.Num(),20);
    TMap<FString,int64> Profiles;for(const auto& P:Read.Snapshot->Profiles)Profiles.Add(P.CharacterId,P.Revision);
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10FullImport")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    O.Fault=MakeShared<TAtomic<EAetherStoreFault>,ESPMode::ThreadSafe>(EAetherStoreFault::AfterFirstWrite);
    auto DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Open isolated destination"),DB.Store.IsValid()))return false;
    TestTrue(TEXT("Interrupted full import rolls back"),DB.Store->ImportLegacy(Import).Get().Code==EAetherStoreCode::Unavailable);
    for(const auto& V:Import.Values)TestTrue(TEXT("No partial converted row"),DB.Store->Read(V.Key).Get().Code==EAetherStoreCode::Missing);
    TestTrue(TEXT("Complete import committed"),DB.Store->ImportLegacy(Import).Get().Code==EAetherStoreCode::Committed);
    DB.Store->Close();DB.Store.Reset();DB=AetherSQLite::Open(O);
    if(!TestTrue(TEXT("Reopen converted database"),DB.Store.IsValid()))return false;
    for(const auto& V:Import.Values)
    {
        const auto Row=DB.Store->Read(V.Key).Get();
        if(!TestTrue(TEXT("Payload and SQL version exactly match imported state"),Row.Value.IsSet()&&Row.Value->Revision==V.Revision&&Row.Value->Payload==V.Payload)){DB.Store->Close();return false;}
        if(V.Key.Kind==EAetherAggregateKind::World)
        {
            FAetherWorldStateV10 W;TestTrue(TEXT("Imported world decodes"),AetherWorldCodec::Decode(Row.Value->Payload,Items,Rules,Profiles,W,Reason));
            TestEqual(TEXT("World version equals row version"),W.Revision,Row.Value->Revision);
        }
        else
        {
            FAetherProfileStateV10 P;TestTrue(TEXT("Imported profile decodes"),AetherProfileCodec::Decode(Row.Value->Payload,Items,Skills,Rules,P,Reason));
            TestEqual(TEXT("Profile version equals row version"),P.Revision,Row.Value->Revision);
        }
    }
    TestTrue(TEXT("Full import replay cannot duplicate pending rewards"),DB.Store->ImportLegacy(Import).Get().Code==EAetherStoreCode::Replayed);DB.Store->Close();
    Read.Snapshot->WindMPerSec=FVector(101,0,0);
    TestFalse(TEXT("Invalid world prevents all profile import output"),AetherLegacyV9::ConvertSnapshot(*Read.Snapshot,Import.SourceSha256,Items,Skills,Rules,Import,Reason));
    TestEqual(TEXT("Previous valid import stays intact"),Import.Values.Num(),20);
    return true;
}
#endif
