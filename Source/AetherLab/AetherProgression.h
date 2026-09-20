#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "AetherCombat.h"
#include "AetherProfile.h"
#include "AetherProgression.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnAetherProfilePublished);

// Player-owned persistent gameplay state and ASC survive avatar replacement.
UCLASS()
class AETHERLAB_API AAetherPlayerState : public APlayerState, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    AAetherPlayerState();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAbilitySystemComponent> AbilitySystem;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAetherAttributes> Attributes;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) FAetherProfile Profile;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) FString DisplayName;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) FString PartyLeader;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) FString InvitedBy;
    // 只在持久化成功发布后（或拥有者收到复制后）通知界面，候选事务不触发刷新。
    FOnAetherProfilePublished OnProfilePublished;
    UFUNCTION() void OnRep_Presentation(){OnProfilePublished.Broadcast();}
    float InvitationExpires=0;
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

