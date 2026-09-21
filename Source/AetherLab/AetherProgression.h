#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "AetherCombat.h"
#include "AetherProfile.h"
#include "Profile/AetherProfileState.h"
#include "AetherProgression.generated.h"

// 拥有者只读授权，不把临时来源写进永久技能账本。
USTRUCT()
struct FAetherSkillGrantPresentation
{
    GENERATED_BODY()
    UPROPERTY() FString SourceId;
    UPROPERTY() FString SkillId;
    UPROPERTY() int32 Rank=1;
    UPROPERTY() uint8 Source=0;
    UPROPERTY() FGuid InstanceId;
    UPROPERTY() double ExpiresAtServerSeconds=0;
};
// 自定义 NetSerialize 将版本、来源及到期时间作为一份原子拥有者快照。
USTRUCT()
struct FAetherSkillGrantSnapshot
{
    GENERATED_BODY()
    UPROPERTY() int64 ProfileRevision=-1;
    UPROPERTY() uint32 Sequence=0;
    UPROPERTY() TArray<FAetherSkillGrantPresentation> Rows;
    bool NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success);
};
template<> struct TStructOpsTypeTraits<FAetherSkillGrantSnapshot> : TStructOpsTypeTraitsBase2<FAetherSkillGrantSnapshot>
{enum {WithNetSerializer=true};};
struct FAetherTemporarySkillSource
{
    FGuid InstanceId;
    double ExpiresAt=0;
};
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
    // 完整 DTO 是服务器读取入口；旧 Profile 仅保留导航需要的只读兼容投影。
    bool PublishNativeProfile(const FAetherProfileStateV10& Committed,FString& Reason);
    const FAetherProfileStateV10* GetNativeProfile() const{return NativeProfile.IsSet()?&NativeProfile.GetValue():nullptr;}
    const FAetherSkillStateV10* GetNativeSkills() const{return bNativeSkillReady&&NativeSkills.IsSet()?&NativeSkills.GetValue():nullptr;}
    TArray<FAetherExternalSkillGrant> GetNativeSkillGrants() const;
    bool GrantRestBlessing(FString& Reason);
    void RefreshTemporarySkills();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) FAetherSkillGrantSnapshot SkillGrants;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) bool bNativeSkillsEnabled=false;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) bool bPartyCaptain=false;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) float InvitationExpires=0;
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    TMap<FString,FAetherTemporarySkillSource> TemporarySkillSources;
    TWeakObjectPtr<APawn> TemporaryGrantAvatar;
    FTimerHandle TemporaryGrantTimer;
    bool bTemporaryPublicationPending=false;
    TOptional<FAetherProfileStateV10> NativeProfile;
    FActiveGameplayEffectHandle NativeEquipmentSource;
    int64 NativeEquipmentRevision=-1;
    bool bPublishingNativeEquipment=false;
    TOptional<FAetherSkillStateV10> NativeSkills;
    TArray<FAetherExternalSkillGrant> NativeSkillGrants;
    int64 NativeSkillRevision=-1;
    bool bNativeSkillReady=false,bPublishingNativeSkills=false;
};

