#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Rendering/Voxel/VoxelRenderResources.h"
#include "VoxelVolume.generated.h"

UCLASS(BlueprintType)
class UVoxelVolume : public UObject
{
    GENERATED_BODY()
public:
    TSharedPtr<struct FVoxelRenderResource> RenderResources;

    UFUNCTION(BlueprintCallable, Category="Voxel")
    void BuildVoxelGrid(const FVector& RegionSize, float BlockSize);

    // Runtime animation helpers
    UFUNCTION(BlueprintCallable, Category="Voxel|Runtime")
    void AnimateScales(float TimeSeconds, float Amplitude = 0.2f, float Frequency = 1.0f);

    UFUNCTION(BlueprintCallable, Category="Voxel|Runtime")
    void AnimateCenters(float TimeSeconds, float Amplitude = 5.0f, float Frequency = 0.5f);

    virtual void BeginDestroy() override;

private:
    // Cached initial layout for runtime animation on GT
    TArray<FVector3f> BaseCenters_GT;
    TArray<float>     BaseScales_GT;
};
