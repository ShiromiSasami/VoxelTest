#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Rendering/Voxel/VoxelVolume.h"
#include "VoxelRenderComponent.generated.h"

using FVoxelDebugHandle = uint32;

UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent))
class UVoxelRenderComponent : public UPrimitiveComponent
{
    GENERATED_BODY()

public:
    UVoxelRenderComponent();
    
    UPROPERTY(EditAnywhere, Category="Voxel")
    TObjectPtr<UVoxelVolume> VolumeAsset = nullptr;
    
    UPROPERTY(EditAnywhere, Category="Voxel", meta=(ClampMin="0.0", UIMin="0.0"))
    FVector Extent = FVector(100.0f);
    
    UPROPERTY(EditAnywhere, Category="Voxel", meta=(ClampMin="1.0", UIMin="1.0"))
    float BlockSize = 20.0f;

    UPROPERTY(EditAnywhere, Category="Voxel|Anim", meta=(EditCondition="bAutoAnimateScales", ClampMin="0.0"))
    float ScaleAmplitude = 0.2f;

    UPROPERTY(EditAnywhere, Category="Voxel|Anim", meta=(EditCondition="bAutoAnimateScales", ClampMin="0.0"))
    float ScaleFrequency = 0.5f;

    UPROPERTY(EditAnywhere, Category="Voxel|Anim", meta=(EditCondition="bAutoAnimateCenters", ClampMin="0.0"))
    float CenterAmplitude = 5.0f; // LS units

    UPROPERTY(EditAnywhere, Category="Voxel|Anim", meta=(EditCondition="bAutoAnimateCenters", ClampMin="0.0"))
    float CenterFrequency = 0.25f;

protected:
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    virtual void OnRegister() override;
    virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport) override;
    virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
    UFUNCTION(BlueprintCallable, Category="Voxel")
    void RebuildFromExtent();


public:
    TSharedPtr<FVoxelRenderResource> GetSharedRenderResources() const
    {
        return VolumeAsset ? VolumeAsset->RenderResources : nullptr;
    }
};
