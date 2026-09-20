#include "Persistence/AetherNativeWorldPhysics.h"
#include "Definitions/AetherV10Definitions.h"
#include "Definitions/AetherWorldDefinition.h"
#include "ReactiveWorldSubsystem.h"
#include "ReactiveBodyComponent.h"
#include "ReactiveMechanismComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
FReactiveSaveRecord ToRuntime(const FAetherReactiveRecordV10& B)
{
    FReactiveSaveRecord V;V.RegionId=FName(*B.RegionId);V.StableId=FName(*B.StableId);V.Transform=B.Transform;
    V.bGateOpen=B.bGateOpen;V.bHasMechanism=B.bHasMechanism;V.bSupportReleased=B.bSupportReleased;V.bSourceEnabled=B.bSourceEnabled;
    V.RemainingEnergyJ=B.RemainingEnergyJ;V.SourceAge=B.SourceAge;V.MaterialSignature=B.MaterialSignature;V.MaterialSchema=B.MaterialSchema;
    V.EnthalpyJ=B.EnthalpyJ;V.WaterKg=B.WaterKg;V.ElectricalWaterKg=B.ElectricalWaterKg;V.ElectricalWetness01=B.ElectricalWetness01;
    V.FuelKg=B.FuelKg;V.Integrity=B.Integrity;V.GasEnergyJ=B.GasEnergyJ;V.bBurning=B.bBurning;V.bBroken=B.bBroken;V.bBurst=B.bBurst;
    return V;
}
FAetherReactiveRecordV10 ToNative(const FReactiveSaveRecord& B)
{
    FAetherReactiveRecordV10 V;V.StableId=B.StableId.ToString();V.Transform=B.Transform;
    // 区域归属根据持久位置重算，避免搬动后沿用旧区域标签。
    V.RegionId=FString::Printf(TEXT("%d_%d"),FMath::FloorToInt(B.Transform.GetLocation().X/7000),FMath::FloorToInt(B.Transform.GetLocation().Y/7000));
    V.bGateOpen=B.bGateOpen;V.bHasMechanism=B.bHasMechanism;V.bSupportReleased=B.bSupportReleased;V.bSourceEnabled=B.bSourceEnabled;
    V.RemainingEnergyJ=B.RemainingEnergyJ;V.SourceAge=B.SourceAge;V.MaterialSignature=B.MaterialSignature;V.MaterialSchema=B.MaterialSchema;
    V.EnthalpyJ=B.EnthalpyJ;V.WaterKg=B.WaterKg;V.ElectricalWaterKg=B.ElectricalWaterKg;V.ElectricalWetness01=B.ElectricalWetness01;
    V.FuelKg=B.FuelKg;V.Integrity=B.Integrity;V.GasEnergyJ=B.GasEnergyJ;V.bBurning=B.bBurning;V.bBroken=B.bBroken;V.bBurst=B.bBurst;
    return V;
}
bool IdentityMap(const TArray<FReactiveSaveRecord>& Rows,TMap<FName,const FReactiveSaveRecord*>& Map)
{
    for(const auto& R:Rows){if(R.StableId.IsNone()||Map.Contains(R.StableId))return false;Map.Add(R.StableId,&R);}
    return true;
}
}
FReactiveSaveRecord AetherNativeWorldPhysics::ToRuntimeRecord(const FAetherReactiveRecordV10& Record){return ToRuntime(Record);}
bool AetherNativeWorldPhysics::RestoreLoaded(UWorld& World,const FAetherWorldStateV10& S,FString& Reason,bool Partial)
{
    check(IsInGameThread());auto* Reactive=World.GetSubsystem<UReactiveWorldSubsystem>();
    const auto& Layout=FAetherWorldDefinitions::Get();
    if(World.GetNetMode()==NM_Client||!Reactive||!Layout.bValid){Reason=TEXT("Authoritative reactive scene/layout unavailable");return false;}
    TArray<FReactiveSaveRecord> Current;
    if(!Reactive->Capture(Current)){Reason=TEXT("Cannot inspect current physical scene");return false;}
    TMap<FName,const FReactiveSaveRecord*> Live;if(!IdentityMap(Current,Live)){Reason=TEXT("Duplicate runtime body identity");return false;}
    TArray<FReactiveSaveRecord> Loaded;TSet<FName> Seen;TMap<FName,bool> Mechanisms;
    for(const auto& B:S.Bodies)
    {
        const FName Key(*B.StableId);
        if(Key.IsNone()||Seen.Contains(Key)){Reason=TEXT("Native body IDs collapse to the same engine identity");return false;}Seen.Add(Key);
        if(Live.Contains(Key)){Loaded.Add(ToRuntime(B));Mechanisms.Add(Key,B.bHasMechanism);}
        else
        {
            // 未加载的已声明流送实体保留在原生聚合，后续加载再局部恢复；未知实体不默默丢弃。
            const auto* Placement=Layout.Find(Key);
            if(!Placement||!Placement->bStream){Reason=TEXT("Saved body is absent from the current map");return false;}
        }
    }
    // Restore 临时关闭模拟；保存原先的动态 primitive，恢复后继续模拟，携带/静态对象不强行开启。
    TArray<TWeakObjectPtr<UPrimitiveComponent>> Resume;
    for(TActorIterator<AActor> It(&World);It;++It)
    {
        TInlineComponentArray<UReactiveBodyComponent*> Bodies;It->GetComponents(Bodies);
        for(auto* Body:Bodies)if(Body&&Seen.Contains(Body->StableId))
        {
            if(const bool* Expected=Mechanisms.Find(Body->StableId);Expected&&*Expected!=(Body->GetOwner()->FindComponentByClass<UReactiveMechanismComponent>()!=nullptr))
            {Reason=TEXT("Saved mechanism topology differs from map");return false;}
            if(auto* Primitive=Body->GetPrimitive();Primitive&&Primitive->IsSimulatingPhysics())Resume.AddUnique(Primitive);
        }
    }
    // 全新世界无历史物理状态，使用地图默认值；不得将旧档/已推进世界的缺失数据当作新档。
    const bool Fresh=S.Revision==0&&S.LegacySaveSchema==0&&S.Bodies.IsEmpty();
    if(!Fresh&&!Reactive->Restore(Loaded,Partial)){Reason=TEXT("Physical snapshot/material/mechanism validation failed");return false;}
    for(const auto& Primitive:Resume)if(Primitive.IsValid())Primitive->SetSimulatePhysics(true);
    if(!Partial&&!Reactive->SetWeather(S.AmbientTemperatureC,S.RainKgPerM2Sec,S.WindMPerSec))
    {Reason=TEXT("Cannot restore authoritative weather");return false;}
    Reason.Reset();return true;
}
bool AetherNativeWorldPhysics::CaptureLoaded(UWorld& World,const FAetherWorldStateV10& Previous,FAetherWorldStateV10& Candidate,FString& Reason)
{
    check(IsInGameThread());auto* Reactive=World.GetSubsystem<UReactiveWorldSubsystem>();
    if(World.GetNetMode()==NM_Client||!Reactive){Reason=TEXT("Authoritative physics unavailable");return false;}
    TArray<FReactiveSaveRecord> Current;
    if(!Reactive->Capture(Current)){Reason=TEXT("Physical capture failed");return false;}
    auto Next=Previous;
    if(!Reactive->GetSimulation()){Reason=TEXT("Physical simulation unavailable");return false;}
    const auto& Environment=Reactive->GetSimulation()->GetEnvironment();
    Next.AmbientTemperatureC=Environment.TemperatureC;Next.RainKgPerM2Sec=Environment.RainKgPerM2Sec;Next.WindMPerSec=Environment.WindMPerSec;
    TMap<FName,int32> Slots;
    for(int32 I=0;I<Next.Bodies.Num();++I)
    {const FName Key(*Next.Bodies[I].StableId);if(Key.IsNone()||Slots.Contains(Key)){Reason=TEXT("Ambiguous stored body identity");return false;}Slots.Add(Key,I);}
    TMap<FName,const FReactiveSaveRecord*> Live;if(!IdentityMap(Current,Live)){Reason=TEXT("Duplicate captured body");return false;}
    for(const auto& R:Current)
    {
        auto B=ToNative(R);
        if(const auto* I=Slots.Find(R.StableId))
        {
            // 引擎 FName 不区分大小写，持久 ID 仍保留原来的规范拼写。
            B.StableId=Next.Bodies[*I].StableId;Next.Bodies[*I]=MoveTemp(B);
        }
        else {Slots.Add(R.StableId,Next.Bodies.Num());Next.Bodies.Add(MoveTemp(B));}
    }
    if(Next.Bodies.Num()>4096){Reason=TEXT("World body capacity exceeded");return false;}
    Next.Bodies.Sort([](const auto& A,const auto& B){return A.StableId.Compare(B.StableId,ESearchCase::CaseSensitive)<0;});
    // 不在此推进 Revision、写数据库或发布世界事实；后续事务仍须复验聚合版本。
    Candidate=MoveTemp(Next);Reason.Reset();return true;
}
