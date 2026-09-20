#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "AetherCombat.h"
#include "AetherProfile.h"
#include "Profile/AetherProfileState.h"
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
    // 原生技能账本只保存在服务器；拥有者通过有界快照通道接收，避免在 Profile 反射旧格式追加字段。
    bool PublishNativeSkills(const FAetherProfileStateV10& Committed,const TArray<FAetherExternalSkillGrant>& Grants,FString& Reason);
    bool RebindNativeSkills(FString& Reason);
    bool PublishNativeEquipment(const FAetherProfileStateV10& Committed,FString& Reason);
    const FAetherSkillStateV10* GetNativeSkills() const{return bNativeSkillReady&&NativeSkills.IsSet()?&NativeSkills.GetValue():nullptr;}
    const TArray<FAetherExternalSkillGrant>& GetNativeSkillGrants() const{return NativeSkillGrants;}
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) bool bNativeSkillsEnabled=false;
    float InvitationExpires=0;
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    FActiveGameplayEffectHandle NativeEquipmentSource;
    int64 NativeEquipmentRevision=-1;
    bool bPublishingNativeEquipment=false;
    TOptional<FAetherSkillStateV10> NativeSkills;
    TArray<FAetherExternalSkillGrant> NativeSkillGrants;
    int64 NativeSkillRevision=-1;
    bool bNativeSkillReady=false,bPublishingNativeSkills=false;
};

