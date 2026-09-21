#include "Misc/AutomationTest.h"
#include "MotionBricksScheduler.h"
#include "HAL/PlatformTime.h"
#include "AetherMotionProfile.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_SERVER
namespace
{
// 使用正式共享调度器和固定发布清单；不会替换 loader、模型或姿态为测试 stub。
class FNativeMotionProbe : public IAutomationLatentCommand
{
public:
    FNativeMotionProbe(FAutomationTestBase* In,EAetherMotionBackend Backend):Test(In),Start(FPlatformTime::Seconds())
    {
        auto& Scheduler=AetherMotionScheduler();Scheduler.Configure(Backend,2);
        World=FGuid::NewGuid();
        for(int32 I=0;I<2;++I)
        {
            Agents[I]=Scheduler.Register();
            FAetherMotionInput Input;Input.AgentId=Agents[I];Input.Style=I==0?TEXT("idle"):TEXT("walk");
            Input.Stamp.WorldEpoch=World;Input.Stamp.PawnEpoch=FGuid::NewGuid();Input.Stamp.RequestSequence=1;
            Input.SpeedMeters=I==0?0:1;Input.Seed=10+I;Stamps[I]=Input.Stamp;
            Test->TestTrue(TEXT("Production scheduler accepts true-model request"),Scheduler.Submit(Input));
        }
    }
    virtual ~FNativeMotionProbe() override{Cleanup();}
    virtual bool Update() override
    {
        auto& Scheduler=AetherMotionScheduler();
        for(int32 I=0;I<2;++I)if(!Done[I])
        {
            FAetherMotionResult Result;if(!Scheduler.Take(Agents[I],Result))continue;
            if(!Result.Clip){Test->AddError(TEXT("Native inference failed: ")+Result.Reason);Done[I]=true;continue;}
            const auto& Clip=*Result.Clip;Test->TestTrue(TEXT("Result keeps world/Pawn/request identity"),Clip.Stamp.SameIntent(Stamps[I])&&Clip.Stamp.RequestSequence==Stamps[I].RequestSequence);
            Test->TestEqual(TEXT("Locked native library produced result"),Clip.NativeRevision,FString(TEXT("ee0cf5d9035f639ed0787f390fb1ce05d6a4c463")));
            Test->TestTrue(TEXT("Finite real inference duration"),Clip.InferenceSeconds>0&&FMath::IsFinite(Clip.InferenceSeconds));
            TArray<FTransform> Pose;UAetherMotionProfile* Default=GetMutableDefault<UAetherMotionProfile>();
            Test->TestTrue(TEXT("UE local pose consumes generated G1 samples"),Clip.Sample(4.5,Pose,Default->Basis())&&Pose.Num()==34);
            Test->AddInfo(FString::Printf(TEXT("MOTION_NATIVE agent=%d pass=%d frames=%u infer_ms=%.3f"),I,Pass[I],Clip.Frames,Clip.InferenceSeconds*1000));
            if(Pass[I]++==0)
            {
                Previous[I]=Result.Clip;
                // 同一 agent 从实际消费的四帧切换风格/方向，覆盖 agent 与无状态 infer 共用模型的路径。
                FAetherMotionInput Next;Next.AgentId=Agents[I];Next.Stamp=Stamps[I];++Next.Stamp.RequestSequence;++Next.Stamp.MovementRevision;
                Next.Context=Clip.Boundary(8);Next.AcceptedSequence=Clip.Stamp.RequestSequence;Next.ConsumedFrameIndex=8;
                Next.Style=I==0?TEXT("walk"):TEXT("idle");Next.SpeedMeters=I==0?1:0;Next.Movement={1,0,0};Next.Facing={1,0,0};Next.Seed=40+I;
                if(I==1)Next.TransitionTarget=Clip.Boundary(Clip.Frames-1);
                Stamps[I]=Next.Stamp;Test->TestTrue(TEXT("Changed intent submitted"),Scheduler.Submit(MoveTemp(Next)));
            }
            else
            {
                Test->TestTrue(TEXT("New native request supplies a new immutable result"),Previous[I].Get()!=Result.Clip.Get());
                Test->TestTrue(TEXT("Boundary remains exactly four G1 frames"),Clip.Boundary(8).IsValid());
                Done[I]=true;
            }
        }
        if(Done[0]&&Done[1]){Cleanup();return true;}
        if(FPlatformTime::Seconds()-Start>120){Test->AddError(TEXT("True UE native inference timed out: ")+Scheduler.Diagnostic());Cleanup();return true;}
        return false;
    }
private:
    void Cleanup()
    {
        for(auto& Id:Agents)if(Id){AetherMotionScheduler().Unregister(Id);FAetherMotionResult Stale;
            Test->TestFalse(TEXT("Released agent cannot deliver a late result"),AetherMotionScheduler().Take(Id,Stale));Id=0;}
    }
    FAutomationTestBase* Test;double Start;FGuid World;uint64 Agents[2]={0,0};FAetherMotionStamp Stamps[2];
    FAetherMotionClipPtr Previous[2];bool Done[2]={false,false};int32 Pass[2]={0,0};
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherMotionCPUIntegration,"Aether.MotionIntegration.TrueNative.CPU",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherMotionCPUIntegration::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FNativeMotionProbe(this,EAetherMotionBackend::CPU));return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherMotionVulkanIntegration,"Aether.MotionIntegration.TrueNative.Vulkan",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherMotionVulkanIntegration::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FNativeMotionProbe(this,EAetherMotionBackend::Vulkan));return true;}
#endif
