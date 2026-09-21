#include "AetherMotionBoundaryAsset.h"
#include "MotionBricksApi.h"
bool UAetherMotionBoundaryAsset::Read(const FString& Expected,FAetherMotionBoundary& Out) const
{
    if(Expected.Len()!=64||SkeletonSha256!=Expected||NativeRevision!=FMotionBricksApi::Revision)return false;
    FAetherMotionBoundary B;B.Roots=Roots;B.Rotations=Rotations;
    if(!B.IsValid())return false;Out=MoveTemp(B);return true;
}
