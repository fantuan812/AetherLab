#pragma once
#include "Subsystems/WorldSubsystem.h"
#include "AetherMotionWorld.generated.h"
UCLASS()
class AETHERMOTIONRUNTIME_API UAetherMotionWorld : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    FGuid Epoch;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override{Super::Initialize(Collection);Epoch=FGuid::NewGuid();}
    virtual void Deinitialize() override{Epoch.Invalidate();Super::Deinitialize();}
};
