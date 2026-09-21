#include "AetherMotionProfile.h"
FMatrix UAetherMotionProfile::Basis() const
{return FMatrix(FPlane(SourceX,0),FPlane(SourceY,0),FPlane(SourceZ,0),FPlane(0,0,0,1));}
bool UAetherMotionProfile::Validate(FString& Why) const
{
    if(Revision<1||SourceMesh.IsNull()||Retargeter.IsNull()||Styles.Num()<2||Styles.Num()>32||
        SourceX.ContainsNaN()||SourceY.ContainsNaN()||SourceZ.ContainsNaN()||
        !FMath::IsNearlyEqual(SourceX.SizeSquared(),1.,1.e-5)||!FMath::IsNearlyEqual(SourceY.SizeSquared(),1.,1.e-5)||!FMath::IsNearlyEqual(SourceZ.SizeSquared(),1.,1.e-5)||
        FMath::Abs(SourceX.Dot(SourceY))>1.e-5||FMath::Abs(SourceX.Dot(SourceZ))>1.e-5||FMath::Abs(SourceY.Dot(SourceZ))>1.e-5||
        !FMath::IsFinite(MaxResultAge)||MaxResultAge<.1f||MaxResultAge>2||!FMath::IsFinite(BlendSeconds)||BlendSeconds<.05f||BlendSeconds>.5f||NativeThreads<1||NativeThreads>8)
    {Why=TEXT("动作配置/正交基无效");return false;}
    for(const auto& P:Styles)
    {if(P.Key.IsNone()||P.Value.IsEmpty()||P.Value.Len()>64){Why=TEXT("无效动作风格");return false;}for(TCHAR C:P.Value)if(!FChar::IsAlnum(C)&&C!='_'){Why=TEXT("无效风格路径");return false;}}
    Why.Reset();return true;
}
