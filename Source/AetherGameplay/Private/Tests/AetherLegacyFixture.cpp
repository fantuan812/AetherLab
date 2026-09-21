#include "Tests/AetherLegacyFixture.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Misc/AutomationTest.h"

bool AetherLegacyFixture::DecodeRope(FReactiveSaveRecord& Record)
{
    FAetherLegacyRecordV6 Old;
    Old.StableId="WorksRope";
    Old.Transform=FTransform(FQuat::Identity,FVector(26900,-650,90),FVector(.2,.2,1.8));
    Old.WaterKg=.025; Old.ElectricalWaterKg=0; Old.ElectricalWetness01=0;
    Old.EnthalpyJ=(.1*1700+.025*4180)*20; Old.FuelKg=.025; Old.Integrity=.6;
    Old.bHasMechanism=true;
    // Frozen schema-0 material parameters and hash order; no call into current Capture/hash code.
    const double Values[]={.1,1700,.05,.05,300,.002,16000000,.2,.01,4,.1,30,.25,0,150000};
    uint32 Hash=GetTypeHash(false);
    for(double V:Values)Hash=HashCombineFast(Hash,GetTypeHash(V));
    Old.MaterialSignature=Hash;
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes,true);
    FObjectAndNameAsStringProxyArchive Out(Writer,false); Out.ArNoDelta=true;
    FAetherLegacyRecordV6::StaticStruct()->SerializeTaggedProperties(Out,reinterpret_cast<uint8*>(&Old),nullptr,nullptr);
    FMemoryReader Reader(Bytes,true);
    FObjectAndNameAsStringProxyArchive In(Reader,false);
    Record=FReactiveSaveRecord();
    FReactiveSaveRecord::StaticStruct()->SerializeTaggedProperties(In,reinterpret_cast<uint8*>(&Record),nullptr,nullptr);
    return !Out.IsError()&&!In.IsError()&&Reader.AtEnd();
}
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherLegacyLayout,"Aether.V8.LegacyTaggedRecordMigration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherLegacyLayout::RunTest(const FString&)
{
    FReactiveSaveRecord R;
    TestTrue(TEXT("Frozen tagged fields decode into current layout"),AetherLegacyFixture::DecodeRope(R));
    TestEqual(TEXT("Absent MaterialSchema defaults to zero"),R.MaterialSchema,0);
    TestEqual(TEXT("Rain-only water retains mass"),R.WaterKg,.025);
    TestEqual(TEXT("Rain-only water does not become electrical water"),R.ElectricalWaterKg,0.);
    TestEqual(TEXT("Rain-only wetness does not become electrical wetness"),R.ElectricalWetness01,0.);
    TestEqual(TEXT("Stored enthalpy retained"),R.EnthalpyJ,5490.);
    TestEqual(TEXT("Stored integrity retained"),R.Integrity,.6);
    return true;
}
#endif
