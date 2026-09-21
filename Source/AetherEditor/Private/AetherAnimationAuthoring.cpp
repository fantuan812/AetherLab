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
#include "TwoBoneIK.h"
namespace
{
struct FControlledPoseKey {double Time=0;FVector Translation=FVector::ZeroVector;FQuat Rotation=FQuat::Identity;};
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
    bool PlantFeet=false;O->TryGetBoolField(TEXT("plantFeet"),PlantFeet);
    const auto& Ref=Source->GetSkeleton()->GetReferenceSkeleton();TMap<FName,TArray<FControlledPoseKey>> Curves;
    for(const auto& Pair:(*Tracks)->Values)
    {
        const FName Bone(*Pair.Key);if(Ref.FindBoneIndex(Bone)==INDEX_NONE)return nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(!Pair.Value->TryGetArray(Rows)||Rows->Num()<2||Rows->Num()>32)return nullptr;
        auto& Keys=Curves.Add(Bone);double Last=-1;
        for(const auto& Row:*Rows)
        {
            const TArray<TSharedPtr<FJsonValue>>* K=nullptr;FControlledPoseKey Key;FVector Rotation;
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
    TArray<TArray<FVector3f>> Positions,Scales;TArray<TArray<FQuat4f>> Rotations;
    Positions.SetNum(Ref.GetNum());Scales.SetNum(Ref.GetNum());Rotations.SetNum(Ref.GetNum());
    for(int32 Frame=0;Frame<=Frames;++Frame)
    {
        const double Fraction=double(Frame)/Frames;
        const double SourceFraction=Animate?(Reverse?1-Fraction:Fraction):0;
        TArray<FTransform> Local,Global,Original;Local.SetNum(Ref.GetNum());Global.SetNum(Ref.GetNum());Original.SetNum(Ref.GetNum());
        for(int32 Bone=0;Bone<Ref.GetNum();++Bone)
        {
            const FName Name=Ref.GetBoneName(Bone);const int32 Parent=Ref.GetParentIndex(Bone);
            FTransform Pose=Model->IsValidBoneTrackName(Name)?Model->EvaluateBoneTrackTransform(Name,Model->GetFrameRate().AsFrameTime(SourceFraction*Model->GetPlayLength()),EAnimInterpolationType::Linear):Ref.GetRefBonePose()[Bone];
            // 根位移归 CharacterMovement；源姿态同样去掉水平根运动后才记录足接触目标。
            if(Bone==0){Pose.SetTranslation(FVector::ZeroVector);Pose.SetRotation(FQuat::Identity);}
            Original[Bone]=Parent>=0?Pose*Original[Parent]:Pose;
            if(const auto* Keys=Curves.Find(Name))
            {
                int32 Next=1;while(Next<Keys->Num()-1&&(*Keys)[Next].Time<Fraction)++Next;
                const auto& A=(*Keys)[Next-1];const auto& B=(*Keys)[Next];const double Alpha=FMath::Clamp((Fraction-A.Time)/(B.Time-A.Time),0.,1.);
                Pose.AddToTranslation(FMath::Lerp(A.Translation,B.Translation,Alpha));
                Pose.SetRotation((Pose.GetRotation()*FQuat::Slerp(A.Rotation,B.Rotation,Alpha)).GetNormalized());
            }
            Local[Bone]=Pose;Global[Bone]=Parent>=0?Pose*Global[Parent]:Pose;
        }
        if(PlantFeet)
        {
            // Manny 左右腿的局部轴并不镜像。以源动画的足位置/方向为约束做解析 IK，
            // 降低骨盆后重新解髋膝，保留走路交替落脚和固定骨长，不能靠猜正负欧拉角造蹲姿。
            for(const TCHAR* Side:{TEXT("l"),TEXT("r")})
            {
                const int32 Hip=Ref.FindBoneIndex(FName(*FString::Printf(TEXT("thigh_%s"),Side)));
                const int32 Knee=Ref.FindBoneIndex(FName(*FString::Printf(TEXT("calf_%s"),Side)));
                const int32 Foot=Ref.FindBoneIndex(FName(*FString::Printf(TEXT("foot_%s"),Side)));
                if(Hip==INDEX_NONE||Knee==INDEX_NONE||Foot==INDEX_NONE){Why=TEXT("Foot contact recipe requires Manny leg chains");return nullptr;}
                FTransform H=Global[Hip],K=Global[Knee],F=Global[Foot];
                const FVector Target=Original[Foot].GetTranslation();
                // 官方人物资产前方为 +Y（运行时 Mesh 再旋转到角色 +X）。
                AnimationCore::SolveTwoBoneIK(H,K,F,H.GetTranslation()+FVector(0,150,0),Target,false,1.,1.);
                F.SetRotation(Original[Foot].GetRotation());
                if(FVector::Dist(F.GetTranslation(),Target)>.1){Why=TEXT("Authored pelvis drop exceeds leg reach");return nullptr;}
                Local[Hip]=H.GetRelativeTransform(Global[Ref.GetParentIndex(Hip)]);
                Local[Knee]=K.GetRelativeTransform(H);Local[Foot]=F.GetRelativeTransform(K);
                // 再从局部变换求全身，保证脚趾、辅助骨和另一条腿使用最新父变换。
                for(int32 Bone=0;Bone<Ref.GetNum();++Bone){const int32 Parent=Ref.GetParentIndex(Bone);Global[Bone]=Parent>=0?Local[Bone]*Global[Parent]:Local[Bone];}
            }
        }
        for(int32 Bone=0;Bone<Ref.GetNum();++Bone)
        {
            if(Local[Bone].ContainsNaN()){Why=TEXT("Invalid authored transform");return nullptr;}
            Positions[Bone].Add(FVector3f(Local[Bone].GetTranslation()));Rotations[Bone].Add(FQuat4f(Local[Bone].GetRotation()));Scales[Bone].Add(FVector3f(Local[Bone].GetScale3D()));
        }
    }
    for(int32 Bone=0;Bone<Ref.GetNum();++Bone)
    {
        const FName Name=Ref.GetBoneName(Bone);
        if(!Result->GetDataModel()->IsValidBoneTrackName(Name))OK&=Controller.AddBoneCurve(Name,false);
        OK&=Controller.SetBoneTrackKeys(Name,Positions[Bone],Rotations[Bone],Scales[Bone],false);
    }
    Controller.NotifyPopulated();Controller.CloseBracket(false);
    if(!OK){Why=TEXT("Failed writing bone tracks");return nullptr;}
    Result->PostEditChange();Result->MarkPackageDirty();if(New)FAssetRegistryModule::AssetCreated(Result);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    if(!UPackage::SavePackage(Result->GetOutermost(),Result,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Args))
    {Why=TEXT("Failed saving controlled animation");return nullptr;}
    Why.Reset();return Result;
}
