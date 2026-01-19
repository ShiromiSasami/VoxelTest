#include "Actors/VoxelActor.h"
#include "Rendering/Voxel/VoxelRenderComponent.h"
#include "Rendering/Voxel/VoxelVolume.h"

AVoxelActor::AVoxelActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
    VoxelComponent = CreateDefaultSubobject<UVoxelRenderComponent>(TEXT("VoxelRenderComponent"));
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    VoxelComponent->SetupAttachment(RootComponent);

    // Minimal default: create a test volume so something renders
    UVoxelVolume* DefaultVolume = CreateDefaultSubobject<UVoxelVolume>(TEXT("VolumeAsset"));
    if (DefaultVolume)
    {
        VoxelComponent->VolumeAsset = DefaultVolume;
        DefaultVolume->BuildVoxelGrid(FVector(100), 20.f);
    }

}
