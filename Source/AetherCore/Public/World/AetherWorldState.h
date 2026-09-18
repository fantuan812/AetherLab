#pragma once
#include "CoreMinimal.h"
#include "Inventory/AetherInventoryState.h"
#include "Definitions/AetherRules.h"

// 保存权威物理标量；温度/冰比例等派生显示值由反应模拟恢复，不写入第二份真相。
struct FAetherReactiveRecordV10
{
    FString RegionId,StableId;
    FTransform Transform=FTransform::Identity;
    bool bGateOpen=false,bHasMechanism=false,bSupportReleased=false,bSourceEnabled=true;
    double RemainingEnergyJ=0,SourceAge=0;
    uint32 MaterialSignature=0;
    int32 MaterialSchema=0;
    double EnthalpyJ=0,WaterKg=0,ElectricalWaterKg=0,ElectricalWetness01=0,FuelKg=0,Integrity=1,GasEnergyJ=0;
    bool bBurning=false,bBroken=false,bBurst=false;
};
struct FAetherWorldLootV10
{
    FGuid ClaimId;
    FVector Location=FVector::ZeroVector;
    // 冻结旧掉落的两种表达：非空 Items 为实际内容，否则使用 Definition/Count。
    FString Definition=TEXT("Material"),ClaimedBy;
    int32 Count=1;
    TMap<FString,int32> Items;
};
struct FAetherCampReceiptV10 { FString Definition;FGuid Instance;int64 RespawnAfterUtc=0; };
struct FAetherLegacyServiceReceiptV9
{
    FGuid CommandId;
    FString TargetId,CharacterId;
    int32 ExpectedRevision=0;
};
struct FAetherEncounterStateV10
{
    FString Definition;
    FGuid Instance;
    uint8 Phase=0; // 冻结语义：Idle/Front/Channel/Elite/Boss/Succeeded/Failed。
    int32 Version=0,Wave=0,LockedSeats=2;
    float PhaseStarted=0,Progress=0;
    TArray<FString> Participants,Settled;
};
struct AETHERCORE_API FAetherWorldStateV10
{
    int64 Revision=0;
    bool bSupplyRestored=false,bWorkshopRestored=false,bBridgeReleased=false,bPowerOn=true;
    double RainKgPerM2Sec=0,AmbientTemperatureC=20;
    FVector WindMPerSec=FVector::ZeroVector;
    TArray<FAetherReactiveRecordV10> Bodies;
    TArray<FAetherWorldLootV10> Loot;
    TArray<FAetherCampReceiptV10> CampReceipts;
    TArray<FAetherLegacyServiceReceiptV9> LegacyServiceReceipts;
    TMap<FString,FString> WorldFactSources;
    FAetherEncounterStateV10 Abbey,Relay;
    int32 LegacySaveSchema=0,LegacyGeneration=0;
    FString LegacySourceSha256;

    // ProfileRevisions 用于跨角色的旧服务回执检查。实际材质/地图对应仍须恢复适配器复验。
    bool Validate(const FAetherV10ItemDefinitions& Items,const FAetherRules& Rules,
        const TMap<FString,int64>& ProfileRevisions,FString& Reason) const;
};
