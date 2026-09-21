#include "MotionBricksModelOwner.h"
#include "HAL/PlatformTime.h"
namespace
{
bool Status(mb_status Code,const char* Error,FString& Why)
{if(Code==MB_OK)return true;Why=FString::Printf(TEXT("MotionBricks %u: %s"),Code,UTF8_TO_TCHAR(Error));return false;}
}
bool FMotionBricksModelOwner::Initialize(EAetherMotionBackend Backend,uint32 Threads,FString& Why)
{
    char Error[1024]={};if(Backend==EAetherMotionBackend::Traditional){Why=TEXT("已选择传统动画");return false;}
    if(!Api.Load(Why))return false;
#define MB_CHECK(Call) if(!Status((Call),Error,Why))return false
    MB_CHECK(Api.mb_runtime_options_create(&Options,Error,sizeof(Error)));
    MB_CHECK(Api.mb_runtime_options_set_device(Options,Backend==EAetherMotionBackend::CPU?MB_DEVICE_CPU:MB_DEVICE_VULKAN,Error,sizeof(Error)));
    MB_CHECK(Api.mb_runtime_options_set_threads(Options,FMath::Clamp(Threads,1u,8u),Error,sizeof(Error)));
    FTCHARToUTF8 Directory(*Api.RuntimeRoot);MB_CHECK(Api.mb_runtime_options_set_backend_directory(Options,Directory.Get(),Error,sizeof(Error)));
    FTCHARToUTF8 Bundle(*(Api.RuntimeRoot/TEXT("g1-f32")));MB_CHECK(Api.mb_model_load(Bundle.Get(),Options,&Model,Error,sizeof(Error)));
    uint32 Count=0;MB_CHECK(Api.mb_model_get_joint_count(Model,&Count,Error,sizeof(Error)));
    if(Count!=34){Why=TEXT("模型不是 G1Skeleton34");return false;}
    Skeleton=MakeShared<FAetherMotionSkeleton,ESPMode::ThreadSafe>();TSet<FName> Names;
    for(uint32 J=0;J<Count;++J)
    {
        const char* Name=nullptr;int32 Parent=-1;float X=0,Y=0,Z=0;
        MB_CHECK(Api.mb_model_get_joint_name(Model,J,&Name,Error,sizeof(Error)));
        MB_CHECK(Api.mb_model_get_joint_parent(Model,J,&Parent,Error,sizeof(Error)));
        MB_CHECK(Api.mb_model_get_neutral_joint_position(Model,J,&X,&Y,&Z,Error,sizeof(Error)));
        if(!Name||Parent>=int32(J)||Parent< -1||(J==0)!=(Parent==-1)||!FMath::IsFinite(X)||!FMath::IsFinite(Y)||!FMath::IsFinite(Z))
        {Why=TEXT("模型骨架层级或参考位置无效");return false;}
        const FName Joint(UTF8_TO_TCHAR(Name));if(Joint.IsNone()||Names.Contains(Joint)){Why=TEXT("模型骨骼名称重复");return false;}
        Names.Add(Joint);Skeleton->Names.Add(Joint);Skeleton->Parents.Add(Parent);Skeleton->NeutralMeters.Add(FVector3f(X,Y,Z));
    }
#undef MB_CHECK
    return true;
}
mb_style* FMotionBricksModelOwner::Style(const FString& Id,FString& Why)
{
    if(auto** Existing=Styles.Find(Id))return *Existing;
    if(Styles.Num()>=32||Id.IsEmpty()||Id.Len()>64){Why=TEXT("风格数量超限");return nullptr;}
    for(TCHAR C:Id)if(!FChar::IsAlnum(C)&&C!='_'){Why=TEXT("无效风格身份");return nullptr;}
    const FString File=TEXT("styles/")+Id+TEXT(".mbstyle");
    if(!Api.VerifiedFiles.Contains(File)){Why=TEXT("风格不在已验证包内：")+Id;return nullptr;}
    char Error[1024]={};mb_style* Value=nullptr;FTCHARToUTF8 Path(*(Api.RuntimeRoot/File));
    if(!Status(Api.mb_style_load(Model,Path.Get(),&Value,Error,sizeof(Error)),Error,Why))return nullptr;
    Styles.Add(Id,Value);return Value;
}
void FMotionBricksModelOwner::RemoveAgent(uint64 Id)
{if(auto* A=Agents.Find(Id)){Api.mb_agent_free(A->Handle);Agents.Remove(Id);}}
FAetherMotionClipPtr FMotionBricksModelOwner::Copy(mb_motion* Native,const FAetherMotionInput& Input,FString& Why)
{
    char Error[1024]={};uint64 Frames=0,Joints=0,RootCount=0,RotationCount=0;const float *Roots=nullptr,*Rotations=nullptr;
    if(!Status(Api.mb_motion_get_frame_count(Native,&Frames,Error,sizeof(Error)),Error,Why)||
        !Status(Api.mb_motion_get_joint_count(Native,&Joints,Error,sizeof(Error)),Error,Why)||
        !Status(Api.mb_motion_get_root_translations(Native,&Roots,&RootCount,Error,sizeof(Error)),Error,Why)||
        !Status(Api.mb_motion_get_local_rotations_xyzw(Native,&Rotations,&RotationCount,Error,sizeof(Error)),Error,Why)||
        Frames<24||Frames>64||Joints!=34||RootCount!=Frames*3||RotationCount!=Frames*136||!Roots||!Rotations)
    {if(Why.IsEmpty())Why=TEXT("生成输出形状不符合固定契约");return {};}
    auto Clip=MakeShared<FAetherMotionClip,ESPMode::ThreadSafe>();Clip->Stamp=Input.Stamp;Clip->SimulationTime=Input.SimulationTime;
    Clip->NativeRevision=FMotionBricksApi::Revision;Clip->Frames=uint32(Frames);Clip->Skeleton=Skeleton;
    Clip->Roots.Append(Roots,int32(RootCount));Clip->Rotations.Append(Rotations,int32(RotationCount));
    for(float V:Clip->Roots)if(!FMath::IsFinite(V)||FMath::Abs(V)>10000){Why=TEXT("生成根轨迹非法");return {};}
    for(int32 I=0;I<Clip->Rotations.Num();I+=4)
    {
        FQuat4f Q(Clip->Rotations[I],Clip->Rotations[I+1],Clip->Rotations[I+2],Clip->Rotations[I+3]);
        if(Q.ContainsNaN()||FMath::Abs(Q.SizeSquared()-1)>.02f){Why=TEXT("生成旋转未正规化");return {};}
    }
    return Clip;
}
FAetherMotionClipPtr FMotionBricksModelOwner::Generate(const FAetherMotionInput& I,FString& Why)
{
    if(!Model){Why=TEXT("模型未载入");return {};}
    char Error[1024]={};mb_motion* Motion=nullptr;const double Started=FPlatformTime::Seconds();
    if(I.TransitionTarget.IsSet())
    {
        if(!I.Context.IsValid()||!I.TransitionTarget->IsValid()){Why=TEXT("衔接动作需要两组四帧边界");return {};}
        mb_inference_request* Request=nullptr;
        if(!Status(Api.mb_inference_request_create(&Request,Error,sizeof(Error)),Error,Why))return {};
        bool OK=Status(Api.mb_inference_request_set_boundary_poses(Request,Model,0,I.Context.Roots.GetData(),12,I.Context.Rotations.GetData(),544,Error,sizeof(Error)),Error,Why)&&
            Status(Api.mb_inference_request_set_boundary_poses(Request,Model,1,I.TransitionTarget->Roots.GetData(),12,I.TransitionTarget->Rotations.GetData(),544,Error,sizeof(Error)),Error,Why)&&
            Status(Api.mb_inference_request_set_seed(Request,I.Seed,Error,sizeof(Error)),Error,Why);
        if(OK)OK=Status(Api.mb_model_infer(Model,Request,&Motion,Error,sizeof(Error)),Error,Why);
        Api.mb_inference_request_free(Request);if(!OK)return {};
    }
    else
    {
        auto* SelectedStyle=Style(I.Style,Why);if(!SelectedStyle)return {};
        auto* Existing=Agents.Find(I.AgentId);
        if(Existing&&!Existing->Stamp.SameLifetime(I.Stamp)){RemoveAgent(I.AgentId);Existing=nullptr;}
        if(!Existing)
        {
            if(Agents.Num()>=16){Why=TEXT("动作代理预算已满");return {};}
            FAgent A;A.Stamp=I.Stamp;
            if(!Status(Api.mb_agent_create(Model,&A.Handle,Error,sizeof(Error)),Error,Why))return {};
            if(!Status(Api.mb_agent_reset(A.Handle,SelectedStyle,Error,sizeof(Error)),Error,Why)){Api.mb_agent_free(A.Handle);return {};}
            Agents.Add(I.AgentId,A);Existing=Agents.Find(I.AgentId);
        }
        // advance 只消费实际播放过的整帧。迟到/废弃计划不能成为下一次生成的历史。
        if(I.AcceptedSequence==Existing->LastSequence&&I.ConsumedFrameIndex>=Existing->Advanced&&Existing->LastSequence!=0)
        {
            if(!Status(Api.mb_agent_advance(Existing->Handle,I.ConsumedFrameIndex-Existing->Advanced,Error,sizeof(Error)),Error,Why)){RemoveAgent(I.AgentId);return {};}
            Existing->Advanced=I.ConsumedFrameIndex;
        }
        else if(Existing->LastSequence!=0&&!I.Context.IsValid())
        {RemoveAgent(I.AgentId);return Generate(I,Why);}
        if(I.Context.IsValid()&&!Status(Api.mb_agent_set_context(Existing->Handle,I.Context.Roots.GetData(),I.Context.Rotations.GetData(),4,34,Error,sizeof(Error)),Error,Why))
        {RemoveAgent(I.AgentId);return {};}
        mb_command* Command=nullptr;
        if(!Status(Api.mb_command_create(&Command,Error,sizeof(Error)),Error,Why))return {};
        bool OK=Status(Api.mb_command_set_style(Command,SelectedStyle,Error,sizeof(Error)),Error,Why)&&
            Status(Api.mb_command_set_movement_direction(Command,I.Movement.X,I.Movement.Y,I.Movement.Z,Error,sizeof(Error)),Error,Why)&&
            Status(Api.mb_command_set_facing_direction(Command,I.Facing.X,I.Facing.Y,I.Facing.Z,Error,sizeof(Error)),Error,Why)&&
            Status(Api.mb_command_set_target_speed(Command,I.SpeedMeters,Error,sizeof(Error)),Error,Why)&&
            Status(Api.mb_command_set_seed(Command,I.Seed,Error,sizeof(Error)),Error,Why);
        if(OK)OK=Status(Api.mb_agent_plan(Existing->Handle,Command,&Motion,Error,sizeof(Error)),Error,Why);
        Api.mb_command_free(Command);
        if(!OK){RemoveAgent(I.AgentId);return {};}
        Existing->LastSequence=I.Stamp.RequestSequence;Existing->Stamp=I.Stamp;Existing->Advanced=0;
    }
    auto Result=Copy(Motion,I,Why);Api.mb_motion_free(Motion);
    if(Result)const_cast<FAetherMotionClip*>(Result.Get())->InferenceSeconds=FPlatformTime::Seconds()-Started;
    return Result;
}
FMotionBricksModelOwner::~FMotionBricksModelOwner()
{
    for(auto& Pair:Agents)Api.mb_agent_free(Pair.Value.Handle);
    Agents.Reset();for(auto& Pair:Styles)Api.mb_style_free(Pair.Value);Styles.Reset();
    if(Model)Api.mb_model_free(Model);if(Options)Api.mb_runtime_options_free(Options);
}
