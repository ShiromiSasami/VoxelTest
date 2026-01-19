#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RHI.h"
#include "RHIResources.h"

// Minimal voxel render payload: only placement data
// - Center: local-space center position of the voxel
// - Scale:  uniform scale (edge length)
struct FVoxelRenderResource : public FRenderResource
{
    TArray<FVector3f>  Centers;
    TArray<float>      Scales;

    FVector3f VolumeMinLS = FVector3f::ZeroVector;
    FVector3f VolumeMaxLS = FVector3f::ZeroVector;
    float     VoxelSizeLS = 0.0f;

    bool IsValid() const
    {
        return Centers.Num() == Scales.Num() && Centers.Num() > 0;
    }

    void InitializeInstances(const TArray<FVector3f>& InCenters, const TArray<float>& InScales)
    {
        Centers    = InCenters;
        Scales     = InScales;
    }

    void ReleaseAll()
    {
        Centers.Reset();
        Scales.Reset();
    }
};
