#include "AetherAnimationAuthoring.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Editor.h"
namespace
{
struct FKey {double Time=0;FVector Translation=FVector::ZeroVector;FQuat Rotation=FQuat::Identity;};
bool Vector(const TSharedPtr<FJsonValue>& Value,FVector& V)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!Value->TryGetArray(A)||A->Num()!=3)return false;
    for(int32 I=0;I<3;++I){double X=0;if(!(*A)[I]->TryGetNumber(X)||!FMath::IsFinite(X)||FMath::Abs(X)>360)return false;V[I]=X;}return true;
}
}
UAnimSequence* UAetherAnimationAuthoring::BakeControlledClip(UAnimSequence* Source,const FString& Recipe,const FString& Path,FString& Why)
{
    Why=TEXT("Invalid controlled animation recipe");
    if((GEditor&&GEditor->PlayWorld)||!Source||!Source->GetSkeleton()||!Source->GetDataModel()||Recipe.Len()>262144||
       !Path.StartsWith(TEXT("/Game/Animation/Controlled/"))||!FPackageName::IsValidLongPackageName(Path))return nullptr;
    TSharedPtr<FJsonObject> O;double Duration=0,Schema=0;bool Animate=false,Reverse=false;
    const TSharedPtr<FJsonObject>* Tracks=nullptr;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Recipe),O)||!O||
       !O->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1||!O->TryGetNumberField(TEXT("duration"),Duration)||!FMath::IsFinite(Duration)||Duration<.1||Duration>5||
       !O->TryGetBoolField(TEXT("animateSource"),Animate)||!O->TryGetBoolField(TEXT("reverseSource"),Reverse)||!O->TryGetObjectField(TEXT("tracks"),Tracks)||(*Tracks)->Values.Num()>64)return nullptr;
    const auto& Ref=Source->GetSkeleton()->GetReferenceSkeleton();TMap<FName,TArray<FKey>> Curves;
    for(const auto& Pair:(*Tracks)->Values)
    {
        const FName Bone(*Pair.Key);if(Ref.FindBoneIndex(Bone)==INDEX_NONE)return nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(!Pair.Value->TryGetArray(Rows)||Rows->Num()<2||Rows->Num()>32)return nullptr;
        auto& Keys=Curves.Add(Bone);double Last=-1;
        for(const auto& Row:*Rows)
        {
            const TArray<TSharedPtr<FJsonValue>>* K=nullptr;FKey Key;FVector Rotation;
            if(!Row->TryGetArray(K)||K->Num()!=3||!(*K)[0]->TryGetNumber(Key.Time)||!FMath::IsFinite(Key.Time)||
               Key.Time<0||Key.Time>1||Key.Time<=Last||!Vector((*K)[1],Rotation)||!Vector((*K)[2],Key.Translation))return nullptr;
            Key.Rotation=FRotator(Rotation.X,Rotation.Y,Rotation.Z).Quaternion();Last=Key.Time;Keys.Add(Key);
        }
        if(Keys[0].Time!=0||Keys.Last().Time!=1)return nullptr;
    }
    // 只覆盖由该配方工具管理的明确输出路径，不修改官方源动画。
    const FString ObjectPath=Path+TEXT(".")+FPackageName::GetLongPackageAssetName(Path);
    UAnimSequence* Result=LoadObject<UAnimSequence>(nullptr,*ObjectPath);
    const bool New=Result==nullptr;
    if(New){auto* Package=CreatePackage(*Path);Package->FullyLoad();Result=NewObject<UAnimSequence>(Package,*FPackageName::GetLongPackageAssetName(Path),RF_Public|RF_Standalone);}
    if(!Result){Why=TEXT("Unable to allocate animation");return nullptr;}
    Result->SetSkeleton(Source->GetSkeleton());Result->bEnableRootMotion=false;
    auto& Controller=Result->GetController();Controller.InitializeModel();Controller.OpenBracket(FText::FromString(TEXT("Bake authored controlled action")),false);
    const int32 Frames=FMath::RoundToInt(Duration*30);
    Controller.SetFrameRate(FFrameRate(30,1),false);Controller.SetNumberOfFrames(FFrameNumber(Frames),false);
    const auto* Model=Source->GetDataModel();bool OK=true;
    for(int32 Bone=0;Bone<Ref.GetNum();++Bone)
    {
        const FName Name=Ref.GetBoneName(Bone);TArray<FVector3f> Positions,Scales;TArray<FQuat4f> Rotations;
        const auto* Keys=Curves.Find(Name);
        for(int32 Frame=0;Frame<=Frames;++Frame)
        {
            const double Fraction=double(Frame)/Frames;
            const double SourceFraction=Animate?(Reverse?1-Fraction:Fraction):0;
            FTransform Pose=Model->IsValidBoneTrackName(Name)?Model->EvaluateBoneTrackTransform(Name,Model->GetFrameRate().AsFrameTime(SourceFraction*Model->GetPlayLength()),EAnimInterpolationType::Linear):Ref.GetRefBonePose()[Bone];
            if(Keys)
            {
                int32 Next=1;while(Next<Keys->Num()-1&&(*Keys)[Next].Time<Fraction)++Next;
                const auto& A=(*Keys)[Next-1];const auto& B=(*Keys)[Next];const double Alpha=FMath::Clamp((Fraction-A.Time)/(B.Time-A.Time),0.,1.);
                Pose.AddToTranslation(FMath::Lerp(A.Translation,B.Translation,Alpha));
                Pose.SetRotation((Pose.GetRotation()*FQuat::Slerp(A.Rotation,B.Rotation,Alpha)).GetNormalized());
            }
            // 所有受控位移归 CharacterMovement；资产只保留身体关节的姿态变化。
            if(Bone==0){Pose.SetTranslation(FVector::ZeroVector);Pose.SetRotation(FQuat::Identity);}
            Positions.Add(FVector3f(Pose.GetTranslation()));Rotations.Add(FQuat4f(Pose.GetRotation()));Scales.Add(FVector3f(Pose.GetScale3D()));
        }
        if(!Result->GetDataModel()->IsValidBoneTrackName(Name))OK&=Controller.AddBoneCurve(Name,false);
        OK&=Controller.SetBoneTrackKeys(Name,Positions,Rotations,Scales,false);
    }
    Controller.NotifyPopulated();Controller.CloseBracket(false);
    if(!OK){Why=TEXT("Failed writing bone tracks");return nullptr;}
    Result->PostEditChange();Result->MarkPackageDirty();if(New)FAssetRegistryModule::AssetCreated(Result);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    if(!UPackage::SavePackage(Result->GetOutermost(),Result,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Args))
    {Why=TEXT("Failed saving controlled animation");return nullptr;}
    Why.Reset();return Result;
}
