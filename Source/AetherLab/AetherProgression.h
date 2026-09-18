#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "AetherCombat.h"
#include "AetherRules.h"
#include "AetherProgression.generated.h"

USTRUCT(BlueprintType)
struct FAetherItemStack
{
    GENERATED_BODY()
    UPROPERTY() FGuid InstanceId;
    UPROPERTY() FName DefinitionId;
    UPROPERTY() int32 Count = 0;
};
USTRUCT(BlueprintType)
struct FAetherProfile
{
    GENERATED_BODY()
    UPROPERTY() FString CharacterId;
    UPROPERTY() int32 Revision = 0;
    UPROPERTY() int32 Gold = 0;
    UPROPERTY() int32 Experience = 0;
    UPROPERTY() FGuid LastAbbeyReceipt;
    UPROPERTY() FGuid LastRelayReceipt;
    UPROPERTY() int32 PendingGold = 0;
    UPROPERTY() int32 PendingMaterial = 0;
    bool CollectPending();
    UPROPERTY() uint8 LearnedSpells = 0;
    UPROPERTY() bool bRegistered = false;
    UPROPERTY() bool bCompanion = false;
    UPROPERTY() TArray<FAetherItemStack> Inventory;
    UPROPERTY() TArray<FName> Evidence;
    UPROPERTY() TArray<FName> Claims;
    UPROPERTY() FString DailyDate;
    UPROPERTY() TArray<FName> DailyEvidence;
    UPROPERTY() TArray<FName> DailyClaims;
    void RefreshDaily(const FString& Date);
    bool ClaimDaily(int32 Template);
    UPROPERTY(NotReplicated) TMap<FName, FGuid> Equipped; // Serialized by the profile NetSerialize.
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& Success);
    static int32 MaxStack(FName Id, const FAetherRules& Rules=FAetherRules::Get());
    int32 Count(FName Id) const;
    bool Add(FName Id, int32 Quantity, const FAetherRules& Rules=FAetherRules::Get());
    bool Remove(FName Id, int32 Quantity, const FAetherRules& Rules=FAetherRules::Get());
    bool Split(FGuid Id, int32 Quantity);
    bool Merge(FGuid From, FGuid To, const FAetherRules& Rules=FAetherRules::Get());
    bool Equip(FGuid Id, const FAetherRules& Rules=FAetherRules::Get());
    bool Observe(FName Fact, const FAetherRules& Rules=FAetherRules::Get());
    bool Available(FName Quest,const FAetherRules& Rules=FAetherRules::Get()) const;
    bool Complete(FName Quest,const FAetherRules& Rules=FAetherRules::Get()) const;
    bool Claim(FName Quest, const FAetherRules& Rules=FAetherRules::Get());
    bool TryAutoClaim(FName Quest);
    bool Validate(const FAetherRules& Rules=FAetherRules::Get()) const;
    static FName QuestId(int32 Quest);
    static FString QuestTitle(FName Quest, const FAetherRules& Rules=FAetherRules::Get());
    static TArray<FName> Objectives(FName Quest, const FAetherRules& Rules=FAetherRules::Get());
};

template<> struct TStructOpsTypeTraits<FAetherProfile> : TStructOpsTypeTraitsBase2<FAetherProfile>
{ enum { WithNetSerializer = true }; };

// Player-owned persistent gameplay state and ASC survive avatar replacement.
UCLASS()
class AETHERLAB_API AAetherPlayerState : public APlayerState, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    AAetherPlayerState();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAbilitySystemComponent> AbilitySystem;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAetherAttributes> Attributes;
    UPROPERTY(Replicated) FAetherProfile Profile;
    UPROPERTY(Replicated) FString DisplayName;
    UPROPERTY(Replicated) FString PartyLeader;
    UPROPERTY(Replicated) FString InvitedBy;
    float InvitationExpires=0;
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

