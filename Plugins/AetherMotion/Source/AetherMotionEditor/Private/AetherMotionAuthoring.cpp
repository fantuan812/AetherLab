#include "AetherMotionAuthoring.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "AssetCompilingManager.h"
#include "UObject/UObjectGlobals.h"
#include "Retargeter/IKRetargetProcessor.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
#include "AetherMotionProfile.h"
#include "AetherMotionTypes.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Editor.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "BoneWeights.h"
#include "ReferenceSkeleton.h"
#include "Materials/Material.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/RetargetOps/RootMotionGeneratorOp.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Math/QuatRotationTranslationMatrix.h"
namespace
{
const TCHAR* Revision=TEXT("ee0cf5d9035f639ed0787f390fb1ce05d6a4c463");
FMatrix Basis(){return FMatrix(FPlane(0,1,0,0),FPlane(0,0,1,0),FPlane(1,0,0,0),FPlane(0,0,0,1));}
bool AuthoringAllowed(FString& Why)
{
    if(GEditor&&GEditor->PlayWorld){Why=TEXT("请先停止 PIE，作者操作不能与游戏模型会话并发");return false;}
    return true;
}
TSharedPtr<FJsonObject> Load(const FString& Path,FString& Why)
{
    FString Text;TSharedPtr<FJsonObject> O;
    if(!AuthoringAllowed(Why)||!FFileHelper::LoadFileToString(Text,*Path)||Text.Len()>32*1024*1024||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O)||!O)
    {if(Why.IsEmpty())Why=TEXT("无法读取有界动作 JSON");return {};}
    return O;
}
bool Skeleton(const TSharedPtr<FJsonObject>& O,FAetherMotionSkeleton& S,FString& Why)
{
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;FString R,Name,Units,Axes;double Fps=0;
    if(!O||!O->TryGetStringField(TEXT("nativeRevision"),R)||R!=Revision||
       !O->TryGetStringField(TEXT("skeleton"),Name)||Name!=TEXT("g1skel34")||
       !O->TryGetStringField(TEXT("units"),Units)||Units!=TEXT("meters")||
       !O->TryGetStringField(TEXT("coordinates"),Axes)||Axes!=TEXT("X-right,Y-up,Z-forward")||
       !O->TryGetNumberField(TEXT("fps"),Fps)||Fps!=30||!O->TryGetArrayField(TEXT("joints"),Rows)||Rows->Num()!=34)
    {Why=TEXT("骨架版本、单位或拓扑不受支持");return false;}
    TSet<FName> Unique;
    for(int32 I=0;I<34;++I)
    {
        const auto J=(*Rows)[I]->AsObject();double P=0;FString N;const TArray<TSharedPtr<FJsonValue>>* V=nullptr;
        if(!J||!J->TryGetStringField(TEXT("name"),N)||N.IsEmpty()||N.Len()>64||
           !J->TryGetNumberField(TEXT("parent"),P)||P!=FMath::FloorToDouble(P)||P>=I||P < -1||(I>0&&P<0)||
           !J->TryGetArrayField(TEXT("neutral"),V)||V->Num()!=3||Unique.Contains(FName(N)))
        {Why=TEXT("关节名称或父层级无效");return false;}
        FVector3f Position;
        for(int32 K=0;K<3;++K){double X=0;if(!(*V)[K]->TryGetNumber(X)||!FMath::IsFinite(X)||FMath::Abs(X)>10){Why=TEXT("中立骨架坐标无效");return false;}Position[K]=float(X);}
        Unique.Add(FName(N));S.Names.Add(FName(N));S.Parents.Add(int32(P));S.NeutralMeters.Add(Position);
    }
    return true;
}
template<class T>T* Asset(const FString& Path,FString& Why)
{
    if(!Path.StartsWith(TEXT("/Game/Animation/Motion/"))||!FPackageName::IsValidLongPackageName(Path))
    {Why=TEXT("动作作者输出必须位于 /Game/Animation/Motion");return nullptr;}
    // 已存在的作品不被无条件重写；明确删除/改名后再制作。
    if(LoadObject<T>(nullptr,*(Path+TEXT(".")+FPackageName::GetLongPackageAssetName(Path))))
    {Why=TEXT("目标资产已存在：")+Path;return nullptr;}
    UPackage* P=CreatePackage(*Path);P->FullyLoad();
    return NewObject<T>(P,*FPackageName::GetLongPackageAssetName(Path),RF_Public|RF_Standalone);
}
bool Save(UObject* O,FString& Why)
{
    if(!O)return false;FAssetRegistryModule::AssetCreated(O);O->MarkPackageDirty();
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    const FString File=FPackageName::LongPackageNameToFilename(O->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    if(!UPackage::SavePackage(O->GetOutermost(),O,*File,Args)){Why=TEXT("资产保存失败：")+O->GetPathName();return false;}
    return true;
}
bool Match(const FAetherMotionSkeleton& S,USkeletalMesh* Mesh,FString& Why)
{
    if(!Mesh||!Mesh->GetSkeleton()||Mesh->GetRefSkeleton().GetNum()!=34){Why=TEXT("必须使用生成的 34 骨 G1 source");return false;}
    const auto& Ref=Mesh->GetRefSkeleton();
    for(int32 I=0;I<34;++I)
    {
        FVector Offset=FVector(S.NeutralMeters[I]);if(S.Parents[I]>=0)Offset-=FVector(S.NeutralMeters[S.Parents[I]]);
        if(Ref.GetBoneName(I)!=S.Names[I]||Ref.GetParentIndex(I)!=S.Parents[I]||
           !Ref.GetRefBonePose()[I].Equals(FTransform(Basis().TransformVector(Offset)*100.),.01f))
        {Why=TEXT("源骨架绑定姿态与锁定模型不匹配");return false;}
    }
    return true;
}
bool Clip(const TSharedPtr<FJsonObject>& O,FAetherMotionClip& C,FString& Why)
{
    auto S=MakeShared<FAetherMotionSkeleton,ESPMode::ThreadSafe>();if(!Skeleton(O,*S,Why))return false;C.Skeleton=S;
    const TArray<TSharedPtr<FJsonValue>> *Roots=nullptr,*Rotations=nullptr;
    if(!O->TryGetArrayField(TEXT("roots"),Roots)||!O->TryGetArrayField(TEXT("rotations"),Rotations)||
       Roots->Num()<4||Roots->Num()>1800||Rotations->Num()!=Roots->Num()){Why=TEXT("动作帧数无效");return false;}
    C.Frames=Roots->Num();
    for(int32 I=0;I<Roots->Num();++I)
    {
        const auto& P=(*Roots)[I]->AsArray();const auto& Q=(*Rotations)[I]->AsArray();
        if(P.Num()!=3||Q.Num()!=136){Why=TEXT("姿态维度无效");return false;}
        for(const auto& V:P){double X=0;if(!V->TryGetNumber(X)||!FMath::IsFinite(X)||FMath::Abs(X)>10000)return false;C.Roots.Add(float(X));}
        for(const auto& V:Q){double X=0;if(!V->TryGetNumber(X)||!FMath::IsFinite(X))return false;C.Rotations.Add(float(X));}
        for(int32 J=0;J<34;++J)
        {const float* F=C.Rotations.GetData()+I*136+J*4;const FQuat4f Rotation(F[0],F[1],F[2],F[3]);if(FMath::Abs(Rotation.SizeSquared()-1)>.02f){Why=TEXT("动作四元数不合法");return false;}}
    }
    return true;
}
TSharedPtr<FJsonValue> Number(double V){return MakeShared<FJsonValueNumber>(V);}
}
USkeletalMesh* UAetherMotionAuthoring::CreateSource(const FString& Json,FString& Why)
{
    Why.Reset();FAetherMotionSkeleton S;if(!Skeleton(Load(Json,Why),S,Why))return nullptr;
    auto* Mesh=Asset<USkeletalMesh>(TEXT("/Game/Animation/Motion/SK_G1MotionSource"),Why);if(!Mesh)return nullptr;
    auto* Rig=Asset<USkeleton>(TEXT("/Game/Animation/Motion/SKEL_G1MotionSource"),Why);if(!Rig)return nullptr;
    Mesh->SetSkeleton(Rig);
    FMeshDescription Description;FSkeletalMeshAttributes Attributes(Description);Attributes.Register();
    Attributes.GetVertexInstanceUVs().SetNumChannels(1);
    const FPolygonGroupID Group=Description.CreatePolygonGroup();Attributes.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("Source");
    FBox Bounds(ForceInit);
    {
    // modifier 析构会重建最终骨架数组；必须先完成再让 Skeleton 读取父链。
    FReferenceSkeletonModifier Ref(Mesh->GetRefSkeleton(),Rig);
    for(int32 I=0;I<34;++I)
    {
        const FVector Global=Basis().TransformVector(FVector(S.NeutralMeters[I]))*100.;
        FVector Offset=Global;if(S.Parents[I]>=0)Offset-=Basis().TransformVector(FVector(S.NeutralMeters[S.Parents[I]]))*100.;
        const FTransform Local(Offset);Ref.Add(FMeshBoneInfo(S.Names[I],S.Names[I].ToString(),S.Parents[I]),Local);
        const FBoneID Bone=Attributes.CreateBone();Attributes.GetBoneNames()[Bone]=S.Names[I];Attributes.GetBoneParentIndices()[Bone]=S.Parents[I];Attributes.GetBonePoses()[Bone]=Local;
        // 每根骨骼都拥有真实蒙皮三角形，Cook/LOD 不会把无权重 source 链裁掉；网格在运行时隐藏。
        TArray<FVertexInstanceID> Corners;
        const FVector Delta[3]={FVector(-.5,0,0),FVector(.5,0,0),FVector(0,0,1)};
        for(int32 K=0;K<3;++K)
        {
            const FVertexID V=Description.CreateVertex();Attributes.GetVertexPositions()[V]=FVector3f(Global+Delta[K]);Bounds+=Global+Delta[K];
            Attributes.GetVertexSkinWeights().Set(V,{UE::AnimationCore::FBoneWeight(I,1.f)});
            const FVertexInstanceID VI=Description.CreateVertexInstance(V);Corners.Add(VI);
            Attributes.GetVertexInstanceUVs().Set(VI,0,FVector2f(K==1?1.f:0.f,K==2?1.f:0.f));
            Attributes.GetVertexInstanceNormals()[VI]=FVector3f(0,-1,0);
            Attributes.GetVertexInstanceTangents()[VI]=FVector3f(1,0,0);
            Attributes.GetVertexInstanceBinormalSigns()[VI]=1;
        }
        Description.CreatePolygon(Group,Corners);
    }
    }
    Mesh->GetMaterials().Add(FSkeletalMaterial(UMaterial::GetDefaultMaterial(MD_Surface),true,false,TEXT("Source")));
    Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());
    Mesh->AddLODInfo();Mesh->CreateMeshDescription(0,MoveTemp(Description));
    if(!Mesh->CommitMeshDescription(0)){Why=TEXT("源 LOD 提交失败");return nullptr;}
    Mesh->SetImportedBounds(FBoxSphereBounds(Bounds));Rig->MergeAllBonesToBoneTree(Mesh);
    Mesh->CalculateInvRefMatrices();Mesh->Build();Mesh->PostEditChange();
    if(!Save(Rig,Why)||!Save(Mesh,Why))return nullptr;return Mesh;
}
bool UAetherMotionAuthoring::CreateRetargetAssets(USkeletalMesh* Source,USkeletalMesh* Target,const FString& Body,FString& Why)
{
    Why.Reset();if(!AuthoringAllowed(Why)||!Source||!Target||(Body!=TEXT("Manny")&&Body!=TEXT("Quinn"))){Why=TEXT("人物或源骨架无效");return false;}
    struct FChain{const TCHAR* Name;const TCHAR* Start;const TCHAR* End;const TCHAR* TargetStart;const TCHAR* TargetEnd;};
    const FChain Chains[]={
        {TEXT("Spine"),TEXT("waist_yaw_skel"),TEXT("waist_pitch_skel"),TEXT("spine_01"),TEXT("spine_05")},
        {TEXT("LeftArm"),TEXT("left_shoulder_pitch_skel"),TEXT("left_wrist_yaw_skel"),TEXT("upperarm_l"),TEXT("hand_l")},
        {TEXT("RightArm"),TEXT("right_shoulder_pitch_skel"),TEXT("right_wrist_yaw_skel"),TEXT("upperarm_r"),TEXT("hand_r")},
        {TEXT("LeftLeg"),TEXT("left_hip_pitch_skel"),TEXT("left_ankle_roll_skel"),TEXT("thigh_l"),TEXT("foot_l")},
        {TEXT("RightLeg"),TEXT("right_hip_pitch_skel"),TEXT("right_ankle_roll_skel"),TEXT("thigh_r"),TEXT("foot_r")},
        {TEXT("LeftToe"),TEXT("left_toe_base"),TEXT("left_toe_base"),TEXT("ball_l"),TEXT("ball_l")},
        {TEXT("RightToe"),TEXT("right_toe_base"),TEXT("right_toe_base"),TEXT("ball_r"),TEXT("ball_r")}};
    for(const auto& C:Chains)
        if(Source->GetRefSkeleton().FindBoneIndex(C.Start)==INDEX_NONE||Source->GetRefSkeleton().FindBoneIndex(C.End)==INDEX_NONE||
           Target->GetRefSkeleton().FindBoneIndex(C.TargetStart)==INDEX_NONE||Target->GetRefSkeleton().FindBoneIndex(C.TargetEnd)==INDEX_NONE)
        {Why=TEXT("重定向链包含不存在的骨骼：")+FString(C.Name);return false;}
    auto* SourceRig=LoadObject<UIKRigDefinition>(nullptr,TEXT("/Game/Animation/Motion/IK_G1.IK_G1"));
    const bool NewSource=!SourceRig;
    if(!SourceRig)SourceRig=Asset<UIKRigDefinition>(TEXT("/Game/Animation/Motion/IK_G1"),Why);
    auto* TargetRig=Asset<UIKRigDefinition>(TEXT("/Game/Animation/Motion/IK_")+Body,Why);
    if(!SourceRig||!TargetRig)return false;
    auto* SC=UIKRigController::GetController(SourceRig);auto* TC=UIKRigController::GetController(TargetRig);
    if(!SC->SetSkeletalMesh(Source)||!TC->SetSkeletalMesh(Target)||!SC->SetRetargetRoot(TEXT("pelvis_skel"))||!TC->SetRetargetRoot(TEXT("pelvis")))
    {Why=TEXT("IK Rig 骨架初始化失败");return false;}
    if(NewSource)for(const auto& C:Chains)SC->AddRetargetChain(C.Name,C.Start,C.End,NAME_None);
    for(const auto& C:Chains)TC->AddRetargetChain(C.Name,C.TargetStart,C.TargetEnd,NAME_None);
    auto* Retarget=Asset<UIKRetargeter>(TEXT("/Game/Animation/Motion/RTG_G1_")+Body,Why);
    auto* Reverse=Asset<UIKRetargeter>(TEXT("/Game/Animation/Motion/RTG_")+Body+TEXT("_G1"),Why);
    if(!Retarget||!Reverse)return false;
    for(auto* R:{Retarget,Reverse})
    {
        const bool Back=R==Reverse;auto* Controller=UIKRetargeterController::GetController(R);
        Controller->SetIKRig(ERetargetSourceOrTarget::Source,Back?TargetRig:SourceRig);
        Controller->SetIKRig(ERetargetSourceOrTarget::Target,Back?SourceRig:TargetRig);
        Controller->SetPreviewMesh(ERetargetSourceOrTarget::Source,Back?Target:Source);
        Controller->SetPreviewMesh(ERetargetSourceOrTarget::Target,Back?Source:Target);
        Controller->AddDefaultOps();
        for(const auto& C:Chains)if(!Controller->SetSourceChain(C.Name,C.Name)){Why=TEXT("重定向链映射失败");return false;}
        Controller->AutoAlignAllBones(ERetargetSourceOrTarget::Target);
    }
    auto* Profile=Asset<UAetherMotionProfile>(TEXT("/Game/Animation/Motion/DA_Motion")+Body,Why);if(!Profile)return false;
    Profile->SourceMesh=Source;Profile->Retargeter=Retarget;
    Profile->Styles={{TEXT("Idle"),TEXT("idle")},{TEXT("Walk"),TEXT("walk")},{TEXT("Injured"),TEXT("injured_walk")},
                     {TEXT("StrafeLeft"),TEXT("walk_left")},{TEXT("StrafeRight"),TEXT("walk_right")},{TEXT("Combat"),TEXT("walk_boxing")}};
    // 蹲姿只有自制 crouch 经质量验收后才加入 profile；不能把上游潜行标签当成碰撞高度证明。
    return (!NewSource||Save(SourceRig,Why))&&Save(TargetRig,Why)&&Save(Retarget,Why)&&Save(Reverse,Why)&&Save(Profile,Why);
}
UAnimSequence* UAetherMotionAuthoring::ImportClip(const FString& Json,USkeletalMesh* Source,const FString& Path,FString& Why)
{
    Why.Reset();FAetherMotionClip C;if(!Clip(Load(Json,Why),C,Why)||!Match(*C.Skeleton,Source,Why))return nullptr;
    if(!Path.StartsWith(TEXT("/Game/Animation/Motion/Baked/"))||!FPackageName::IsValidLongPackageName(Path)){Why=TEXT("Import output must be an authored Baked clip");return nullptr;}
    // 已管理的派生片段可重复导入，避免源 JSON 更新而 uasset 仍保留旧骨盆高度。
    auto* Animation=LoadObject<UAnimSequence>(nullptr,*(Path+TEXT(".")+FPackageName::GetLongPackageAssetName(Path)));
    if(Animation&&Animation->GetSkeleton()!=Source->GetSkeleton()){Why=TEXT("Existing clip has a different skeleton");return nullptr;}
    if(!Animation)Animation=Asset<UAnimSequence>(Path,Why);if(!Animation)return nullptr;Animation->SetSkeleton(Source->GetSkeleton());
    auto& Controller=Animation->GetController();Controller.InitializeModel();Controller.OpenBracket(FText::FromString(TEXT("导入 G1 真实推理姿态")),false);
    Controller.SetFrameRate(FFrameRate(30,1),false);Controller.SetNumberOfFrames(FFrameNumber(int32(C.Frames)-1),false);
    TArray<TArray<FVector3f>> Positions;TArray<TArray<FQuat4f>> Rotations;Positions.SetNum(34);Rotations.SetNum(34);
    TArray<FTransform> Pose;
    for(uint32 F=0;F<C.Frames;++F)
    {
        C.Sample(F,Pose,Basis());
        // 编辑器烘焙保留完整根轨迹。运行时 Sample 单独抑制水平位移，避免移动权威重复施加。
        Pose[0].SetTranslation(Basis().TransformVector(FVector(C.Roots[F*3],C.Roots[F*3+1],C.Roots[F*3+2]))*100.);
        for(int32 J=0;J<34;++J){Positions[J].Add(FVector3f(Pose[J].GetTranslation()));Rotations[J].Add(FQuat4f(Pose[J].GetRotation()));}
    }
    TArray<FVector3f> Scale;Scale.Init(FVector3f::OneVector,C.Frames);bool OK=true;
    for(int32 J=0;J<34;++J){if(!Animation->GetDataModel()->IsValidBoneTrackName(C.Skeleton->Names[J]))OK&=Controller.AddBoneCurve(C.Skeleton->Names[J],false);OK&=Controller.SetBoneTrackKeys(C.Skeleton->Names[J],Positions[J],Rotations[J],Scale,false);}
    Controller.NotifyPopulated();Controller.CloseBracket(false);
    if(!OK){Why=TEXT("动画轨道写入失败");return nullptr;}Animation->PostEditChange();return Save(Animation,Why)?Animation:nullptr;
}
bool UAetherMotionAuthoring::ExportClip(UAnimSequence* Animation,USkeletalMesh* Source,const FString& Json,const FString& Output,FString& Why)
{
    Why.Reset();ON_SCOPE_EXIT{if(!Why.IsEmpty())UE_LOG(LogTemp,Error,TEXT("AETHER_MOTION_EXPORT_FAIL %s"),*Why);};
    FAssetCompilingManager::Get().FinishAllCompilation();auto O=Load(Json,Why);FAetherMotionSkeleton S;
    if(!Skeleton(O,S,Why)||!Match(S,Source,Why)||!Animation||Animation->GetSkeleton()!=Source->GetSkeleton()){Why=TEXT("导出前必须把动画重定向到 G1 source");return false;}
    const auto* Model=Animation->GetDataModel();if(!Model||Model->GetPlayLength()<=0||Model->GetPlayLength()>60){Why=TEXT("只导出 0 至 60 秒的有效动作");return false;}
    const int32 Frames=FMath::FloorToInt(Model->GetPlayLength()*30)+1;if(Frames<4||Frames>1800)return false;
    TArray<TSharedPtr<FJsonValue>> Roots,Rotations;const FMatrix B=Basis(),Inverse=B.InverseFast();
    for(int32 F=0;F<Frames;++F)
    {
        TArray<TSharedPtr<FJsonValue>> P,Q;
        const FFrameTime Time=Model->GetFrameRate().AsFrameTime(F/30.);
        for(int32 J=0;J<34;++J)
        {
            const FTransform T=Model->IsValidBoneTrackName(S.Names[J])?Model->EvaluateBoneTrackTransform(S.Names[J],Time,EAnimInterpolationType::Linear):Source->GetRefSkeleton().GetRefBonePose()[J];
            if(T.ContainsNaN()||!T.GetScale3D().Equals(FVector::OneVector,.001)){Why=TEXT("风格包含无效变换或非单位缩放");return false;}
            if(J==0){const FVector V=Inverse.TransformVector(T.GetTranslation()/100.);P={Number(V.X),Number(V.Y),Number(V.Z)};}
            else if(!T.GetTranslation().Equals(Source->GetRefSkeleton().GetRefBonePose()[J].GetTranslation(),.05))
            {Why=FString::Printf(TEXT("G1 non-root translation: bone=%s frame=%d pose=%s bind=%s"),*S.Names[J].ToString(),F,*T.GetTranslation().ToString(),*Source->GetRefSkeleton().GetRefBonePose()[J].GetTranslation().ToString());return false;}
            const FQuat R=FQuat(B*FQuatRotationMatrix(T.GetRotation())*Inverse).GetNormalized();
            Q.Append({Number(R.X),Number(R.Y),Number(R.Z),Number(R.W)});
        }
        Roots.Add(MakeShared<FJsonValueArray>(P));Rotations.Add(MakeShared<FJsonValueArray>(Q));
    }
    // 风格来源必须对应已经落盘的资源字节，不能对未保存动画记录旧文件摘要。
    if(Animation->GetOutermost()->IsDirty()){Why=TEXT("导出前请先保存源动画");return false;}
    FString SourceFile;
    if(!FPackageName::DoesPackageExist(Animation->GetOutermost()->GetName(),&SourceFile)){Why=TEXT("源动画包尚未保存");return false;}
    TArray<uint8> SourceBytes;
    if(!FFileHelper::LoadFileToArray(SourceBytes,*SourceFile)){Why=TEXT("无法读取源动画包");return false;}
    uint8 Digest[SHA256_DIGEST_LENGTH];SHA256(SourceBytes.GetData(),SourceBytes.Num(),Digest);
    O->SetStringField(TEXT("sourceFileSha256"),BytesToHex(Digest,SHA256_DIGEST_LENGTH).ToLower());
    O->SetArrayField(TEXT("roots"),Roots);O->SetArrayField(TEXT("rotations"),Rotations);O->SetStringField(TEXT("sourceAsset"),Animation->GetPathName());
    FString Text;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    if(!FFileHelper::SaveStringToFile(Text,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)){Why=TEXT("姿态文件写入失败");return false;}return true;
}

UAnimSequence* UAetherMotionAuthoring::RetargetClip(UAnimSequence* Animation,USkeletalMesh* Source,USkeletalMesh* Target,UIKRetargeter* Retargeter,const FString& Path,FString& Why)
{
    Why.Reset();
    if(!AuthoringAllowed(Why)||!Animation||!Source||!Target||!Retargeter||Animation->GetSkeleton()!=Source->GetSkeleton()||
       !Path.StartsWith(TEXT("/Game/Animation/Motion/Authored/"))){Why=TEXT("Invalid offline retarget inputs");return nullptr;}
    // 先完成源资产加载，再写独立序列；避免复制源 DataModel 的 OnEndLoad 通知与目标压缩任务相撞。
    FlushAsyncLoading();FAssetCompilingManager::Get().FinishAllCompilation();
    FRetargetProfile Profile;Profile.FillProfileWithAssetSettings(Retargeter);
    FRetargetInitParameters Init;Init.SourceSkeletalMesh=Source;Init.TargetSkeletalMesh=Target;Init.RetargeterAsset=Retargeter;Init.CustomProfile=&Profile;
    FIKRetargetProcessor Processor;Processor.Initialize(Init);
    if(!Processor.IsInitialized()){Why=TEXT("Cannot initialize offline IK retargeter");return nullptr;}
    const auto* Model=Animation->GetDataModel();if(!Model||Model->GetPlayLength()<=0||Model->GetPlayLength()>60){Why=TEXT("Invalid source duration");return nullptr;}
    const int32 Frames=FMath::RoundToInt(Model->GetPlayLength()*30);
    const auto& Ref=Source->GetRefSkeleton();const auto& TargetRef=Target->GetRefSkeleton();
    TArray<TArray<FVector3f>> Positions,Scales;TArray<TArray<FQuat4f>> Rotations;
    Positions.SetNum(TargetRef.GetNum());Scales.SetNum(TargetRef.GetNum());Rotations.SetNum(TargetRef.GetNum());
    double MaxProjectionCm=0;
    for(int32 Frame=0;Frame<=Frames;++Frame)
    {
        TArray<FTransform> Global;Global.SetNum(Ref.GetNum());
        const auto Time=Model->GetFrameRate().AsFrameTime(Frame/30.);
        for(int32 Bone=0;Bone<Ref.GetNum();++Bone)
        {
            const FName Name=Ref.GetBoneName(Bone);const int32 Parent=Ref.GetParentIndex(Bone);
            const FTransform Local=Model->IsValidBoneTrackName(Name)?Model->EvaluateBoneTrackTransform(Name,Time,EAnimInterpolationType::Linear):Ref.GetRefBonePose()[Bone];
            Global[Bone]=Parent>=0?Local*Global[Parent]:Local;
        }
        FRetargetRunParameters Run;Run.SourceGlobalPose=&Global;Run.Profile=&Profile;Run.DeltaTime=1.f/30.f;
        const auto& Pose=Processor.RunRetargeter(Run);
        if(Pose.Num()!=TargetRef.GetNum()){Why=TEXT("Retarget output topology differs");return nullptr;}
        TArray<FTransform> Canonical;Canonical.SetNum(Pose.Num());
        for(int32 Bone=0;Bone<Pose.Num();++Bone)
        {
            const int32 Parent=TargetRef.GetParentIndex(Bone);
            FTransform Local=Parent>=0?Pose[Bone].GetRelativeTransform(Pose[Parent]):Pose[Bone];
            // G1 模型只表示根平移和关节转动。重定向可能移动髋链起点，作者阶段投影回固定骨长，
            // 同时测量全局关节误差并限幅；不能静默丢弃任意非刚性动画。
            if(Parent>=0)Local.SetTranslation(TargetRef.GetRefBonePose()[Bone].GetTranslation());
            Canonical[Bone]=Parent>=0?Local*Canonical[Parent]:Local;
            MaxProjectionCm=FMath::Max(MaxProjectionCm,FVector::Dist(Canonical[Bone].GetTranslation(),Pose[Bone].GetTranslation()));
            if(Local.ContainsNaN()){Why=TEXT("Non-finite retarget pose");return nullptr;}
            Positions[Bone].Add(FVector3f(Local.GetTranslation()));Rotations[Bone].Add(FQuat4f(Local.GetRotation().GetNormalized()));Scales[Bone].Add(FVector3f(Local.GetScale3D()));
        }
    }
    UE_LOG(LogTemp,Display,TEXT("AETHER_G1_PROJECTION max_joint_error_cm=%.4f"),MaxProjectionCm);
    if(MaxProjectionCm>5){Why=FString::Printf(TEXT("G1 fixed-bone projection exceeds 5 cm: %.4f"),MaxProjectionCm);return nullptr;}
    const FString ObjectPath=Path+TEXT(".")+FPackageName::GetLongPackageAssetName(Path);
    auto* Result=LoadObject<UAnimSequence>(nullptr,*ObjectPath);
    if(!Result)Result=Asset<UAnimSequence>(Path,Why);if(!Result)return nullptr;
    Result->SetSkeleton(Target->GetSkeleton());Result->bEnableRootMotion=false;
    auto& Controller=Result->GetController();Controller.InitializeModel();Controller.OpenBracket(FText::FromString(TEXT("Offline IK retarget to G1")),false);
    Controller.SetFrameRate(FFrameRate(30,1),false);Controller.SetNumberOfFrames(FFrameNumber(Frames),false);bool OK=true;
    for(int32 Bone=0;Bone<TargetRef.GetNum();++Bone)
    {
        const FName Name=TargetRef.GetBoneName(Bone);
        if(!Result->GetDataModel()->IsValidBoneTrackName(Name))OK&=Controller.AddBoneCurve(Name,false);
        OK&=Controller.SetBoneTrackKeys(Name,Positions[Bone],Rotations[Bone],Scales[Bone],false);
    }
    Controller.NotifyPopulated();Controller.CloseBracket(false);
    if(!OK){Why=TEXT("Cannot write retargeted bone tracks");return nullptr;}
    Result->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();
    return Save(Result,Why)?Result:nullptr;
}

bool UAetherMotionAuthoring::CalibrateRetarget(UIKRetargeter* Retargeter,USkeletalMesh* G1,bool Reverse,FString& Why)
{
 Why.Reset();if(!AuthoringAllowed(Why)||!Retargeter||!G1||G1->GetRefSkeleton().GetNum()!=34)return false;
 const auto& Ref=G1->GetRefSkeleton();TArray<FTransform> Global;Global.SetNum(Ref.GetNum());double MinZ=0;
 for(int32 I=0;I<Ref.GetNum();++I)
 {
  const int32 Parent=Ref.GetParentIndex(I);Global[I]=Parent<0?Ref.GetRefBonePose()[I]:Ref.GetRefBonePose()[I]*Global[Parent];
  MinZ=FMath::Min(MinZ,Global[I].GetTranslation().Z);
 }
 const double Height=-MinZ;
 if(Height<40||Height>150){Why=TEXT("G1 reference leg height invalid");return false;}
 auto* Controller=UIKRetargeterController::GetController(Retargeter);
 // 模型中立坐标原点在骨盆，UE pelvis op 却按离地高度求体型比例。
 // 只校准 retarget pose 到足底地面，不改锁定的 34 骨模型或推理结果单位。
 const auto Side=Reverse?ERetargetSourceOrTarget::Target:ERetargetSourceOrTarget::Source;
 // UE 的 SetRootOffsetInRetargetPose 接收增量，重复执行必须减去已有偏移，防止身高逐次减半。
 Controller->SetRootOffsetInRetargetPose(FVector(0,0,Height)-Controller->GetRootOffsetInRetargetPose(Side),Side);
 bool FoundRoot=false;
 for(int32 I=0;I<Controller->GetNumRetargetOps();++I)
  if(auto* Root=Cast<UIKRetargetRootMotionController>(Controller->GetOpController(I)))
  {
   FoundRoot=true;
   // G1 的唯一根就是骨盆，反向输出由 pelvis op 负责，不能再被 root op 覆盖。
   Controller->SetRetargetOpEnabled(I,!Reverse);
   if(!Reverse)
   {
    Root->SetSourceRootBone(TEXT("pelvis_skel"));Root->SetTargetRootBone(TEXT("root"));
    auto& Settings=*static_cast<FIKRetargetRootMotionOpSettings*>(Controller->GetRetargetOpByIndex(I)->GetSettings());Settings.TargetPelvis.BoneName=TEXT("pelvis");
    Settings.RootMotionSource=ERootMotionSource::GenerateFromTargetPelvis;Settings.RootHeightSource=ERootMotionHeightSource::SnapToGround;
    Settings.bMaintainOffsetFromPelvis=true;
   }
  }
 if(!FoundRoot){Why=TEXT("Retarget root op missing");return false;}
 Retargeter->PostEditChange();return Save(Retargeter,Why);
}
