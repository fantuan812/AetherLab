#pragma once
#include "CoreMinimal.h"
#include "Animation/AetherActionPresentation.h"
class AAetherCharacter;
// 只读的本帧身体状态，不含 ASC、世界查询、原生推理句柄或业务写入口。
struct FAetherAnimationSnapshot
{
    FVector Velocity=FVector::ZeroVector;
    float Time=0,Direction=0,CastStarted=0,CastUntil=0;
    bool bAlive=false,bStunned=false,bFalling=false,bBlocking=false,bCrouched=false,bAttacking=false,bCarrying=false,bRescuing=false;
    FAetherActionPresentation Action;
};
namespace AetherAnimationSnapshot
{
    FAetherAnimationSnapshot Capture(const AAetherCharacter& Character);
}
