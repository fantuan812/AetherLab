#include "AetherMotionTypes.h"
#include "Math/QuatRotationTranslationMatrix.h"
bool FAetherMotionBoundary::IsValid() const
{
    if(Roots.Num()!=12||Rotations.Num()!=544)return false;
    for(float V:Roots)if(!FMath::IsFinite(V)||FMath::Abs(V)>10000)return false;
    for(int32 I=0;I<Rotations.Num();I+=4)
    {
        const FQuat4f Q(Rotations[I],Rotations[I+1],Rotations[I+2],Rotations[I+3]);
        if(Q.ContainsNaN()||FMath::Abs(Q.SizeSquared()-1)>0.02f)return false;
    }
    return true;
}
FAetherMotionBoundary FAetherMotionClip::Boundary(uint32 Last) const
{
    FAetherMotionBoundary B;if(Frames<4||Roots.Num()!=int32(Frames*3)||Rotations.Num()!=int32(Frames*136))return B;
    Last=FMath::Clamp(Last,3u,Frames-1);
    B.Roots.Append(Roots.GetData()+(Last-3)*3,12);B.Rotations.Append(Rotations.GetData()+(Last-3)*136,544);return B;
}
bool FAetherMotionClip::Sample(double Frame,TArray<FTransform>& Out,const FMatrix& Basis) const
{
    if(!Skeleton||Skeleton->Names.Num()!=34||Skeleton->Parents.Num()!=34||Skeleton->NeutralMeters.Num()!=34||
        Frames<4||Roots.Num()!=int32(Frames*3)||Rotations.Num()!=int32(Frames*136)||!FMath::IsFinite(Frame))return false;
    Frame=FMath::Clamp(Frame,0.,double(Frames-1));const int32 A=FMath::FloorToInt(Frame),B=FMath::Min(A+1,int32(Frames)-1);const double Alpha=Frame-A;
    const FMatrix Inverse=Basis.InverseFast();Out.SetNum(34);
    for(int32 J=0;J<34;++J)
    {
        const float* X=Rotations.GetData()+(A*34+J)*4;const float* Y=Rotations.GetData()+(B*34+J)*4;
        const FQuat Q=FQuat::Slerp(FQuat(X[0],X[1],X[2],X[3]),FQuat(Y[0],Y[1],Y[2],Y[3]),Alpha).GetNormalized();
        // 矩阵共轭适用于标定基；不能把四元数分量随意互换。
        const FQuat Rotation(Inverse*FQuatRotationMatrix(Q)*Basis);
        const int32 Parent=Skeleton->Parents[J];
        FVector Position=FVector(Skeleton->NeutralMeters[J]);
        if(Parent>=0)Position-=FVector(Skeleton->NeutralMeters[Parent]);
        else Position=FVector(0,FMath::Lerp(Roots[A*3+1],Roots[B*3+1],Alpha),0);
        Out[J]=FTransform(Rotation.GetNormalized(),Basis.TransformVector(Position)*100.,FVector::OneVector);
    }
    return true;
}
