#include "Rendering/Voxel/VoxelRenderComponent.h"
#include "Rendering/Voxel/VoxelSceneProxy.h"
#include "Misc/App.h"

UVoxelRenderComponent::UVoxelRenderComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    bTickInEditor = true;
}

FPrimitiveSceneProxy* UVoxelRenderComponent::CreateSceneProxy()
{
    return new FVoxelSceneProxy(this);
}

FBoxSphereBounds UVoxelRenderComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    const FBox LocalBox(-Extent, Extent);
    return FBoxSphereBounds(LocalBox).TransformBy(LocalToWorld);
}

void UVoxelRenderComponent::OnRegister()
{
    Super::OnRegister();
    if (VolumeAsset)
    {
        const FVector RegionSize = Extent;
        VolumeAsset->BuildVoxelGrid(RegionSize, FMath::Max(1.0f, BlockSize));
    }
}

void UVoxelRenderComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
    Super::OnUpdateTransform(UpdateTransformFlags, Teleport);
    MarkRenderTransformDirty();
}

void UVoxelRenderComponent::RebuildFromExtent()
{
    if (!VolumeAsset)
    {
        return;
    }
    const FVector RegionSize = Extent;
    VolumeAsset->BuildVoxelGrid(RegionSize, FMath::Max(1.0f, BlockSize));
    MarkRenderStateDirty();
}

#if WITH_EDITOR
void UVoxelRenderComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    const FName Name = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
    if (Name == GET_MEMBER_NAME_CHECKED(UVoxelRenderComponent, Extent)
        || Name == GET_MEMBER_NAME_CHECKED(UVoxelRenderComponent, BlockSize)
        || Name == GET_MEMBER_NAME_CHECKED(UVoxelRenderComponent, VolumeAsset))
    {
        RebuildFromExtent();
    }
}
#endif

void UVoxelRenderComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!VolumeAsset) return;
    const UWorld* World = GetWorld();
    const float T = (World && World->IsGameWorld()) ? World->GetTimeSeconds() : static_cast<float>(FApp::GetCurrentTime());
    VolumeAsset->AnimateScales(T, ScaleAmplitude, ScaleFrequency);
    VolumeAsset->AnimateCenters(T, CenterAmplitude, CenterFrequency);
}
