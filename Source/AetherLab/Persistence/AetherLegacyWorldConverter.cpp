#include "Persistence/AetherLegacyWorldConverter.h"
#include "Persistence/AetherLegacyProfileConverter.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherWorldCodec.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
namespace
{
const FAetherRules& FrozenRules()
{
    static const FAetherRules D=[]
    {
        FString Json;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/Legacy/V9Rules.json")));
        return FAetherRules::Parse(Json);
    }();return D;
}
FString Text(FName N){return N.IsNone()?FString():N.ToString();}
FString ItemId(FName Name,const FAetherV10ItemDefinitions& Items)
{
    FString Result;
    for(const auto& P:Items.Items)if(FName(*P.Key)==Name){if(!Result.IsEmpty())return {};Result=P.Key;}
    return Result;
}
FAetherEncounterStateV10 Encounter(const FAetherEncounterRun& Old)
{
    FAetherEncounterStateV10 E;
    // 旧 FName 不区分大小写；固定遭遇定义采用规范拼写，未知定义交给验证器拒绝。
    E.Definition=Old.Definition=="Abbey"?TEXT("Abbey"):Old.Definition=="Relay"?TEXT("Relay"):Text(Old.Definition);
    E.Instance=Old.Instance;E.Phase=uint8(Old.Phase);E.Version=Old.Version;E.Wave=Old.Wave;E.LockedSeats=Old.LockedSeats;
    E.PhaseStarted=Old.PhaseStarted;E.Progress=Old.Progress;E.Participants=Old.Participants;E.Settled=Old.Settled;return E;
}
}
bool AetherLegacyV9::ConvertWorld(const UAetherFrontierSave& Old,const FString& Digest,
    const FAetherV10ItemDefinitions& Items,const FAetherRules& Rules,const TMap<FString,int64>& Profiles,
    FAetherWorldStateV10& Out,FString& Reason)
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(Old.World.Num()>4096||Old.Loot.Num()>128||Old.CampReceipts.Num()>32||Old.ServiceReceipts.Num()>64||Old.WorldFacts.Sources.Num()>512)
        return Fail(TEXT("Legacy world collection exceeds conversion bounds"));
    if(!FrozenRules().bValid||!Old.ValidateWorldLedger(FrozenRules()))return Fail(TEXT("Legacy world violates frozen ledger rules"));
    FAetherWorldStateV10 Next;Next.Revision=Old.Generation;Next.LegacyGeneration=Old.Generation;
    Next.LegacySaveSchema=Old.Version;Next.LegacySourceSha256=Digest;
    Next.bSupplyRestored=Old.bSupplyRestored;Next.bWorkshopRestored=Old.bWorkshopRestored;Next.bBridgeReleased=Old.bBridgeReleased;Next.bPowerOn=Old.bPowerOn;
    Next.RainKgPerM2Sec=Old.RainKgPerM2Sec;Next.AmbientTemperatureC=Old.AmbientTemperatureC;Next.WindMPerSec=Old.WindMPerSec;
    // v4 也保留物理记录；不沿用旧登录路径清空 World 后改版本的行为。
    for(const auto& B:Old.World)
    {
        FAetherReactiveRecordV10 V;V.RegionId=Text(B.RegionId);V.StableId=Text(B.StableId);V.Transform=B.Transform;
        V.bGateOpen=B.bGateOpen;V.bHasMechanism=B.bHasMechanism;V.bSupportReleased=B.bSupportReleased;V.bSourceEnabled=B.bSourceEnabled;
        V.RemainingEnergyJ=B.RemainingEnergyJ;V.SourceAge=B.SourceAge;V.MaterialSignature=B.MaterialSignature;V.MaterialSchema=B.MaterialSchema;
        V.EnthalpyJ=B.EnthalpyJ;V.WaterKg=B.WaterKg;V.ElectricalWaterKg=B.ElectricalWaterKg;V.ElectricalWetness01=B.ElectricalWetness01;
        V.FuelKg=B.FuelKg;V.Integrity=B.Integrity;V.GasEnergyJ=B.GasEnergyJ;V.bBurning=B.bBurning;V.bBroken=B.bBroken;V.bBurst=B.bBurst;
        Next.Bodies.Add(MoveTemp(V));
    }
    for(const auto& L:Old.Loot)
    {
        FAetherWorldLootV10 V;V.ClaimId=L.ClaimId;V.Location=L.Location;V.Definition=ItemId(L.Definition,Items);V.Count=L.Count;V.ClaimedBy=L.ClaimedBy;
        for(const auto& P:L.Items)
        {
            const auto Key=ItemId(P.Key,Items);
            if(Key.IsEmpty()||V.Items.Contains(Key))return Fail(TEXT("Unknown/ambiguous legacy loot definition"));
            V.Items.Add(Key,P.Value);
        }
        Next.Loot.Add(MoveTemp(V));
    }
    for(const auto& C:Old.CampReceipts)
    {
        FAetherCampReceiptV10 V;
        for(const auto& P:Rules.Encounters)if(P.Key==C.Definition)V.Definition=P.Key.ToString();
        V.Instance=C.Instance;V.RespawnAfterUtc=C.RespawnAfterUtc;Next.CampReceipts.Add(MoveTemp(V));
    }
    for(const auto& S:Old.ServiceReceipts)
    {
        FAetherLegacyServiceReceiptV9 V;V.CommandId=S.Command.Id;V.TargetId=Text(S.Command.TargetId);
        V.ExpectedRevision=S.Command.ExpectedRevision;V.CharacterId=S.CharacterId;Next.LegacyServiceReceipts.Add(MoveTemp(V));
    }
    for(const auto& F:Old.WorldFacts.Sources)
    {
        const auto* Rule=Rules.Objectives.Find(F.Key);
        if(!Rule||Rule->Scope!=EAetherObjectiveScope::World)return Fail(TEXT("Unknown migrated world objective"));
        FString Source;
        for(FName Candidate:Rule->FactSources)if(Candidate==F.Value)Source=Candidate.ToString();
        if(Source.IsEmpty())return Fail(TEXT("Unknown migrated world fact source"));
        // 使用当前定义的规范 key，不依赖旧 FName 首次出现时的大小写。
        for(const auto& P:Rules.Objectives)if(P.Key==F.Key)Next.WorldFactSources.Add(P.Key.ToString(),Source);
    }
    Next.Abbey=Encounter(Old.Abbey);Next.Relay=Encounter(Old.Relay);
    if(!Next.Validate(Items,Rules,Profiles,Reason))return false;
    Out=MoveTemp(Next);return true;
}
bool AetherLegacyV9::ConvertSnapshot(const UAetherFrontierSave& Old,const FString& Digest,
    const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,
    FAetherLegacyImport& Out,FString& Reason)
{
    if(Old.Profiles.Num()>128){Reason=TEXT("Too many legacy profiles");return false;}
    FAetherLegacyImport Candidate;Candidate.SourceSchema=Old.Version;Candidate.SourceSha256=Digest;
    TMap<FString,int64> Versions;
    for(const auto& OldProfile:Old.Profiles)
    {
        FAetherProfileStateV10 P;
        if(!ConvertProfile(OldProfile,Old.Version,Digest,Items,Skills,Rules,P,Reason))return false;
        if(Versions.Contains(P.CharacterId)){Reason=TEXT("Duplicate legacy character identity");return false;}
        Versions.Add(P.CharacterId,P.Revision);
        FAetherStoredAggregate Row;Row.Key={EAetherAggregateKind::Profile,P.CharacterId};Row.Revision=P.Revision;
        if(!AetherProfileCodec::Encode(P,Items,Skills,Rules,Row.Payload,Reason))return false;
        Candidate.Values.Add(MoveTemp(Row));
    }
    FAetherWorldStateV10 World;FAetherStoredAggregate Row;Row.Key={EAetherAggregateKind::World,TEXT("Main")};
    if(!ConvertWorld(Old,Digest,Items,Rules,Versions,World,Reason))return false;
    Row.Revision=World.Revision;
    if(!AetherWorldCodec::Encode(World,Items,Rules,Versions,Row.Payload,Reason))return false;
    Candidate.Values.Add(MoveTemp(Row));
    if(!AetherImports::Validate(Candidate,Reason))return false;
    // 后台存储只收到完整值对象；旧 UObject 的生命周期不会跨越异步数据库任务。
    Out=MoveTemp(Candidate);return true;
}
