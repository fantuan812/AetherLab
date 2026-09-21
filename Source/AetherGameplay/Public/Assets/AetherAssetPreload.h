#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/StreamableManager.h"
#include "AetherAssetPreload.generated.h"
class UAetherGameContent;
UCLASS()
class UAetherAssetPreload : public UTickableWorldSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 virtual void Tick(float Dt) override;
 virtual TStatId GetStatId() const override {RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherAssetPreload,STATGROUP_Tickables);}
 bool Ready() const;
 UPROPERTY(Transient) TObjectPtr<UAetherGameContent> Content;
private:
 bool bRequested=false,bFailed=false;
 TSharedPtr<FStreamableHandle> RootHandle,SharedHandle;
};
