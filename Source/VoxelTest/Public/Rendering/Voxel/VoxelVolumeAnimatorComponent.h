#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelVolumeAnimatorComponent.generated.h"

class UVoxelRenderComponent;
class UVoxelVolume;

UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent))
class UVoxelVolumeAnimatorComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVoxelVolumeAnimatorComponent();

    UPROPERTY(EditAnywhere, Category="Voxel|Anim")
    bool bAnimateScales = true;

    UPROPERTY(EditAnywhere, Category="Voxel|Anim", meta=(ClampMin="0.0"))
    float ScaleAmplitude = 0.2f;

    UPROPERTY(EditAnywhere, Category="Voxel|Anim", meta=(ClampMin="0.0"))
    float ScaleFrequency = 0.5f;

    UPROPERTY(EditAnywhere, Category="Voxel|Anim")
    bool bAnimateCenters = false;

    UPROPERTY(EditAnywhere, Category="Voxel|Anim", meta=(ClampMin="0.0"))
    float CenterAmplitude = 5.0f; // in local-space units

    UPROPERTY(EditAnywhere, Category="Voxel|Anim", meta=(ClampMin="0.0"))
    float CenterFrequency = 0.25f;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    TWeakObjectPtr<UVoxelVolume> Volume;
};

