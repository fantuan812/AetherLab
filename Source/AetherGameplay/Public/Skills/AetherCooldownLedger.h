#pragma once
#include "Subsystems/GameInstanceSubsystem.h"
#include "AetherCooldownLedger.generated.h"

// Session policy: cooldown advances during disconnect and map travel. Server shutdown ends the session.
UCLASS()
class AETHERGAMEPLAY_API UAetherCooldownLedger : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    TMap<FString,double> Read(const FString& CharacterId);
    void Put(const FString& CharacterId,const FString& Key,double Seconds);
private:
    TMap<FString,TMap<FString,double>> Deadlines;
    void Prune();
};
