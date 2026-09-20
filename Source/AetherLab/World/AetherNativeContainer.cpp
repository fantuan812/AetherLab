#include "World/AetherNativeContainer.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

AAetherNativeContainer::AAetherNativeContainer()
{
    bReplicates=true;SetReplicateMovement(true);
    Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Container"));RootComponent=Mesh;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(Cube.Succeeded())Mesh->SetStaticMesh(Cube.Object);
    Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));Mesh->SetSimulatePhysics(false);
    SetActorScale3D(FVector(.45,.45,.45));
}
bool AAetherNativeContainer::Configure(const FAetherContainerRestoreDescriptor& D)
{
    if(!HasAuthority()||!StableId.IsEmpty()||!D.bActive||D.Id.IsEmpty())return false;
    StableId=D.Id;OwnerCharacterId=D.Owner;RegionId=D.Region;ContainerKind=uint8(D.Kind);Revision=D.Revision;
    bOnlyRelevantToOwner=D.Kind==EAetherContainerKind::PersonalStorage;
    // 私人容器在登录装配时绑定 Owner；没有 Owner 时不会向其他连接复制。
    SetActorLocation(D.Location);ForceNetUpdate();return true;
}
bool AAetherNativeContainer::Allows(const FString& Identity) const
{return !IsActorBeingDestroyed()&&(!bOnlyRelevantToOwner||OwnerCharacterId.Equals(Identity,ESearchCase::CaseSensitive));}
bool AAetherNativeContainer::Publish(const FAetherContainerStateV10& S)
{
    if(!HasAuthority()||!StableId.Equals(S.ContainerId,ESearchCase::CaseSensitive)||S.Revision<Revision||
        !OwnerCharacterId.Equals(S.OwnerCharacterId,ESearchCase::CaseSensitive)||ContainerKind!=uint8(S.Kind))return false;
    Revision=S.Revision;RegionId=S.RegionId;
    if(!S.bActive){Destroy();return true;}
    SetActorLocation(S.Location);ForceNetUpdate();return true;
}
void AAetherNativeContainer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AAetherNativeContainer,StableId);
    DOREPLIFETIME(AAetherNativeContainer,Revision);DOREPLIFETIME(AAetherNativeContainer,ContainerKind);
}
