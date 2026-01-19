#include "Rendering/Voxel/VoxelVolumeAnimatorComponent.h"
#include "Rendering/Voxel/VoxelRenderComponent.h"
#include "Rendering/Voxel/VoxelVolume.h"
#include "Misc/App.h"

UVoxelVolumeAnimatorComponent::UVoxelVolumeAnimatorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    bTickInEditor = true;
}

void UVoxelVolumeAnimatorComponent::BeginPlay()
{
    Super::BeginPlay();
    // Find sibling VoxelRenderComponent to access its VolumeAsset
    if (AActor* Owner = GetOwner())
    {
        if (UVoxelRenderComponent* VoxelComp = Owner->FindComponentByClass<UVoxelRenderComponent>())
        {
            Volume = VoxelComp->VolumeAsset;
        }
    }
}

void UVoxelVolumeAnimatorComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!Volume.IsValid()) return;
    const UWorld* World = GetWorld();
    const float T = (World && World->IsGameWorld()) ? World->GetTimeSeconds() : static_cast<float>(FApp::GetCurrentTime());
    if (bAnimateScales)
    {
        Volume->AnimateScales(T, ScaleAmplitude, ScaleFrequency);
    }
    if (bAnimateCenters)
    {
        Volume->AnimateCenters(T, CenterAmplitude, CenterFrequency);
    }
}

