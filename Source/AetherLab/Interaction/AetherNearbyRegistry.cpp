#include "Interaction/AetherNearbyRegistry.h"
#include "GameFramework/Actor.h"

bool UAetherNearbyRegistry::CellFor(FVector P,FIntVector& Out)
{
    // 有界坐标避免损坏的变换进入整数转换或构造无界查询循环。
    if(P.ContainsNaN()||P.GetAbsMax()>100000000.)return false;
    Out=FIntVector(FMath::FloorToInt(P.X/500.),FMath::FloorToInt(P.Y/500.),FMath::FloorToInt(P.Z/500.));return true;
}
bool UAetherNearbyRegistry::Register(AActor* A)
{
    check(IsInGameThread());FIntVector Cell;
    if(!IsValid(A)||A->GetWorld()!=GetWorld()||!A->GetRootComponent()||!CellFor(A->GetActorLocation(),Cell))return false;
    if(Entries.Contains(A))return true;
    // 固定上限是错误隔离边界，不会为溢出的对象回退到全世界扫描。
    if(Entries.Num()>=8192)return false;
    auto* Root=A->GetRootComponent();Entries.Add(A,{Cell,Root});Cells.FindOrAdd(Cell).Add(A);
    Root->TransformUpdated.AddUObject(this,&UAetherNearbyRegistry::Moved);return true;
}
void UAetherNearbyRegistry::RemoveFromCell(TWeakObjectPtr<AActor> A,FIntVector Cell)
{
    if(auto* Bucket=Cells.Find(Cell)){Bucket->Remove(A);if(Bucket->IsEmpty())Cells.Remove(Cell);}
}
void UAetherNearbyRegistry::Unregister(AActor* A)
{
    check(IsInGameThread());const auto* Found=Entries.Find(A);if(!Found)return;const auto E=*Found;
    if(auto* Root=E.Root.Get())Root->TransformUpdated.RemoveAll(this);
    RemoveFromCell(A,E.Cell);Entries.Remove(A);
}
void UAetherNearbyRegistry::Moved(USceneComponent* Component,EUpdateTransformFlags,ETeleportType)
{
    auto* A=Component?Component->GetOwner():nullptr;auto* E=Entries.Find(A);if(!E||E->Root!=Component)return;
    FIntVector Cell;
    if(!CellFor(A->GetActorLocation(),Cell)){Unregister(A);return;}
    if(Cell==E->Cell)return;
    RemoveFromCell(A,E->Cell);E->Cell=Cell;Cells.FindOrAdd(Cell).Add(A);
}
bool UAetherNearbyRegistry::Contains(const AActor* A) const
{return IsValid(A)&&A->GetWorld()==GetWorld()&&Entries.Contains(const_cast<AActor*>(A));}
TArray<TWeakObjectPtr<AActor>> UAetherNearbyRegistry::Nearby(FVector P,double Radius) const
{
    check(IsInGameThread());TArray<TWeakObjectPtr<AActor>> Out;FIntVector Min,Max;
    if(!FMath::IsFinite(Radius)||Radius<0||Radius>1000||!CellFor(P-FVector(Radius),Min)||!CellFor(P+FVector(Radius),Max))return Out;
    for(int32 X=Min.X;X<=Max.X;++X)for(int32 Y=Min.Y;Y<=Max.Y;++Y)for(int32 Z=Min.Z;Z<=Max.Z;++Z)
        if(const auto* Bucket=Cells.Find(FIntVector(X,Y,Z)))for(const auto& Weak:*Bucket)
            if(const auto* A=Weak.Get();IsValid(A)&&FVector::DistSquared(P,A->GetActorLocation())<=FMath::Square(Radius))Out.Add(Weak);
    return Out;
}
void UAetherNearbyRegistry::Deinitialize()
{
    for(const auto& Pair:Entries)if(auto* Root=Pair.Value.Root.Get())Root->TransformUpdated.RemoveAll(this);
    Entries.Reset();Cells.Reset();Super::Deinitialize();
}
