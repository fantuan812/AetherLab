#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "AetherEquipmentComponent.h"
#include "Animation/AetherActionPresentation.h"
#include "Animation/AetherAnimationSnapshot.h"
#include "AetherAnimation.generated.h"
class UBlendSpace;
class UAnimSequence;
class UAnimMontage;
UENUM(BlueprintType)
enum class EAetherMotionState:uint8 { Grounded, Rising, Falling, Landing, Guarding, Stunned, Downed };
UCLASS(Transient,Blueprintable)
class AETHERGAMEPLAY_API UAetherAnimInstance : public UAnimInstance
{
 GENERATED_BODY()
public:
 UAetherAnimInstance();
 virtual void NativeInitializeAnimation() override;
 virtual void NativeUpdateAnimation(float Dt) override;
 UPROPERTY(BlueprintReadOnly,Transient) EAetherMotionState MotionState=EAetherMotionState::Grounded;
 UPROPERTY(BlueprintReadOnly,Transient) float GroundSpeed=0;
 UPROPERTY(BlueprintReadOnly,Transient) float Direction=0;
 UPROPERTY(BlueprintReadOnly,Transient) float AirWeight=0;
 UPROPERTY(BlueprintReadOnly,Transient) float FootWeight=0;
 UPROPERTY(Transient) TObjectPtr<UAetherActionSet> ActionSet;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> ControlledClip;
 float ControlledTime=0,ControlledWeight=0;
 UPROPERTY(Transient) TObjectPtr<UBlendSpace> Locomotion;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> JumpClip;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> FallClip;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> LandClip;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> HeavyClip;
 UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> LightClips;
 FVector HandTargets[2]={FVector::ZeroVector,FVector::ZeroVector};
 FVector ElbowTargets[2]={FVector::ZeroVector,FVector::ZeroVector};
 float HandWeight=0;
 FVector FootTargets[2]={FVector::ZeroVector,FVector::ZeroVector};
 FVector KneeTargets[2]={FVector::ZeroVector,FVector::ZeroVector};
 static float AttackPosition(const FAetherAttackDefinition& Attack,float Elapsed,float Length);
 static EAetherMotionState SelectState(bool Alive,bool Stunned,bool Falling,float VerticalSpeed,bool Landing,bool Guarding);
protected:
 virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
 virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
private:
 void UpdateLocomotion(const FAetherAnimationSnapshot& Frame,float Dt);
 void UpdateActions(AAetherCharacter& Character,const FAetherAnimationSnapshot& Frame,float Dt);
 void UpdateContacts(AAetherCharacter& Character,const FAetherAnimationSnapshot& Frame,float Dt);
 FName ControlledActionId;
 UPROPERTY(Transient) TObjectPtr<UAnimMontage> AttackMontage;
 uint32 LastAttack=0;
 float LandUntil=0,TraceAt=0;
 bool WasFalling=false,bFeetValid=false,WasAlive=true;
 float LastVerticalSpeed=0,RevivedAt=-100,DownedAt=0;
 FName LandingId=TEXT("Land");
};
