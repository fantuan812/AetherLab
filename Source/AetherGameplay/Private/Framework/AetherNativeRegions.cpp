#include "Framework/AetherFrontier.h"
#include "World/AetherWorldDefinition.h"
#include "Persistence/AetherNativePersistence.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"

void AAetherFrontierMode::AdvanceNativeRegions(const TArray<FName>& Unload,const TArray<const FAetherWorldPlacement*>& Load)
{
    auto* W=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();
    auto* Store=GetGameInstance()->GetSubsystem<UAetherNativePersistence>();
    if(!bNativeSceneReady||!W||!Store)return;
    if(bNativeRegionBarrier)
    {
        if(!NativeRegionOutcome.IsSet())
        {
            if(!NativeRegionSave.IsValid()||!NativeRegionSave.IsReady())return;
            NativeRegionOutcome=NativeRegionSave.Get();NativeRegionSave={};
        }
        const auto& Result=NativeRegionOutcome.GetValue();
        const bool Committed=(Result.Code==EAetherStoreCode::Committed||Result.Code==EAetherStoreCode::Replayed)&&Result.World.IsSet();
        TArray<FName> Resume,Discard;
        // 等待期间玩家可能折返；重新计算的 Unload 才决定是否销毁，绝不沿用旧距离结论。
        for(FName Id:FrozenRegionBodies)if(Committed&&Unload.Contains(Id))Discard.Add(Id);else Resume.Add(Id);
        if(!Resume.IsEmpty()&&!W->ResumeFrozen(Resume))
        {FailNativeScene(TEXT("Cannot resume frozen region after persistence barrier"));return;}
        for(auto& Frozen:FrozenRegionActors)
        {
            auto* A=Frozen.Actor.Get();if(!A){FailNativeScene(TEXT("Frozen region actor disappeared before acknowledgement"));return;}
            if(Committed&&Unload.Contains(A->Spec.Id))
            {A->ResetFragments();Registry.Remove(A->Spec.Id);Props.Remove(A);A->Destroy();++RegionUnloads;continue;}
            A->bEnabled=Frozen.Enabled;A->SetActorTickEnabled(Frozen.Ticking);
            for(const auto& Component:Frozen.TickingComponents)if(Component.IsValid())Component->SetComponentTickEnabled(true);
            if(Frozen.Simulating){A->Mesh->SetSimulatePhysics(true);A->Mesh->SetPhysicsLinearVelocity(Frozen.Linear);A->Mesh->SetPhysicsAngularVelocityInDegrees(Frozen.Angular);}
            A->ForceNetUpdate();
        }
        W->DiscardFrozen(Discard);FrozenRegionActors.Reset();FrozenRegionBodies.Reset();NativeRegionOutcome.Reset();bNativeRegionBarrier=false;
        RebuildWorldLinks();
        // 同一帧不立刻重新冻结失败的卸载；后续正常区域周期再试。
        return;
    }
    if(!Unload.IsEmpty())
    {
        if(Store->IsSavingWorld())return;
        TArray<FReactiveSaveRecord> Capture;if(!W->Capture(Capture))return;
        for(FName Id:Unload)if(auto* A=Prop(Id);A&&A->Reactive->bParticipatesInSimulation)
        {if(!Capture.ContainsByPredicate([&](const auto& R){return R.StableId==Id;})){FrozenRegionBodies.Reset();return;}FrozenRegionBodies.Add(Id);}
        if(!W->FreezeForPersistence(FrozenRegionBodies)){FrozenRegionBodies.Reset();return;}
        for(FName Id:Unload)if(auto* A=Prop(Id))
        {
            FFrozenRegionActor F;F.Actor=A;F.Enabled=A->bEnabled;F.Ticking=A->IsActorTickEnabled();F.Simulating=A->Mesh->IsSimulatingPhysics();
            if(F.Simulating){F.Linear=A->Mesh->GetPhysicsLinearVelocity();F.Angular=A->Mesh->GetPhysicsAngularVelocityInDegrees();A->Mesh->SetSimulatePhysics(false);}
            TInlineComponentArray<UActorComponent*> Components;A->GetComponents(Components);
            for(auto* Component:Components)if(Component->IsComponentTickEnabled()){F.TickingComponents.Add(Component);Component->SetComponentTickEnabled(false);}
            A->bEnabled=false;A->SetActorTickEnabled(false);A->ForceNetUpdate();FrozenRegionActors.Add(MoveTemp(F));
        }
        // 冻结记录仍由 Capture 返回，后台写者不会因为求解器注销而遗漏离线区域。
        bNativeRegionBarrier=true;NativeRegionSave=Store->SaveLoadedPhysics();return;
    }
    if(Load.IsEmpty())return;
    TArray<FReactiveSaveRecord> Restore;TArray<AAetherFrontierProp*> Spawned;
    for(const auto* E:Load)if(auto* A=SpawnPlacement(*E))
    {
        Spawned.Add(A);
        if(const auto* R=Database->World.FindByPredicate([&](const auto& V){return V.StableId==E->Id;}))Restore.Add(*R);

    }
    if(!Restore.IsEmpty()&&!W->Restore(Restore,true))
    {
        for(auto* A:Spawned){Registry.Remove(A->Spec.Id);Props.Remove(A);A->Destroy();}
        RebuildWorldLinks();UE_LOG(LogTemp,Warning,TEXT("AETHER_NATIVE_REGION_RESTORE_DEFERRED"));return;
    }
    for(auto* A:Spawned)
    {
        if(const auto* E=FAetherWorldDefinitions::Get().Find(A->Spec.Id);E&&E->Heat>0&&
            !Database->World.ContainsByPredicate([&](const auto& R){return R.StableId==E->Id;}))
        {FReactiveStimulus Heat;Heat.HeatJ=E->Heat;A->Reactive->Inject(Heat);}
        if(A->bCarryable)A->Mesh->SetSimulatePhysics(true);
        A->bWasBurning=A->Reactive->State.bBurning;A->bExtinguished=A->bInspectableFire&&!A->bWasBurning&&A->Reactive->State.FuelKg<A->Reactive->GetMaterial().InitialFuelKg-1.e-8;
        ++RegionLoads;
    }
    RebuildWorldLinks();
}
